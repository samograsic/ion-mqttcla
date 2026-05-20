/*
	mqttcli.c:	ION MQTT Convergence Layer induct adapter daemon.
			Handles bundle reception via MQTT.

	Author: Samo Grasic <samo@grasic.net>
	Based on: ION TCPCL v4 implementation

	Copyright (c) 2026, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
	acknowledged.
									*/

#include "mqttcli.h"
#include "bpP.h"
#include "llcv.h"
#include <sys/time.h>
#include <pthread.h>

/* Global state */
static MqttClientState	mqttState;
static VInduct		*mqttcli_vduct = NULL;
static int		running = 0;

/* Forward declarations */
static int		mqtt_init(MqttClientState *state, const char *topic, MqttConfig *config);
static void		mqtt_cleanup(MqttClientState *state);

/* MQTT callback: Connection lost */
void mqtt_connection_lost_callback(void *context, char *cause)
{
	writeMemoNote("[!] mqttcli: connection lost", cause ? cause : "unknown reason");

	/* Try to reconnect */
	MqttClientState *state = (MqttClientState *)context;
	if (state && state->running)
	{
		writeMemo("[i] mqttcli: attempting to reconnect...");
		/* Reconnection will be handled in the main loop */
	}
}

/* MQTT callback: Message arrived */
int mqtt_message_arrived_callback(void *context, char *topicName,
		int topicLen, MQTTClient_message *message)
{
	MqttClientState		*state = (MqttClientState *)context;
	AcqWorkArea		*work;
	unsigned char		*payload;
	int			payloadLen;

	if (mqttcli_vduct == NULL || message == NULL)
	{
		writeMemo("[!] mqttcli: invalid state in message callback");
		MQTTClient_freeMessage(&message);
		MQTTClient_free(topicName);
		return 1;
	}

	payload = (unsigned char *)message->payload;
	payloadLen = message->payloadlen;

	writeMemoNote("[i] mqttcli: received message, length", itoa(payloadLen));

	/* Get acquisition work area from ION */
	work = bpGetAcqArea(mqttcli_vduct);
	if (work == NULL)
	{
		putErrmsg("mqttcli: can't get acquisition work area.", NULL);
		MQTTClient_freeMessage(&message);
		MQTTClient_free(topicName);
		return 1;
	}

	/* Begin bundle acquisition */
	if (bpBeginAcq(work, 0, NULL) < 0)
	{
		putErrmsg("mqttcli: can't begin acquisition of bundle.", NULL);
		bpReleaseAcqArea(work);
		MQTTClient_freeMessage(&message);
		MQTTClient_free(topicName);
		return 1;
	}

	/* Continue acquisition with the received bundle data */
	if (bpContinueAcq(work, (char *)payload, payloadLen, NULL, 0) < 0)
	{
		putErrmsg("mqttcli: can't continue acquisition.", NULL);
		bpCancelAcq(work);
		bpReleaseAcqArea(work);
		MQTTClient_freeMessage(&message);
		MQTTClient_free(topicName);
		return 1;
	}

	/* End acquisition */
	if (bpEndAcq(work) < 0)
	{
		putErrmsg("mqttcli: can't end acquisition of bundle.", NULL);
		bpReleaseAcqArea(work);
		MQTTClient_freeMessage(&message);
		MQTTClient_free(topicName);
		return 1;
	}

	writeMemo("[i] mqttcli: bundle received and delivered successfully");

	/* Release resources */
	bpReleaseAcqArea(work);
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);

	return 1; /* Message handled */
}

/* MQTT callback: Delivery complete */
void mqtt_delivery_complete_callback(void *context,
		MQTTClient_deliveryToken dt)
{
	/* Not used for induct, but required by Paho MQTT */
}

/* Initialize MQTT client */
static int mqtt_init(MqttClientState *state, const char *topic, MqttConfig *config)
{
	MQTTClient_connectOptions	conn_opts = MQTTClient_connectOptions_initializer;
	int				rc;
	char				client_id_prefix[128];

	memset(state, 0, sizeof(MqttClientState));

	/* Build client ID prefix */
	snprintf(client_id_prefix, sizeof(client_id_prefix), "%scli_", config->client_id_prefix);

	/* Generate unique client ID */
	mqtt_generate_client_id(state->clientId, sizeof(state->clientId), client_id_prefix);

	/* Store topic */
	if (strlen(topic) >= MQTT_MAX_TOPIC_LEN)
	{
		putErrmsg("mqttcli: topic name too long.", topic);
		return -1;
	}
	strcpy(state->topic, topic);

	/* Store disconnect timeout */
	state->disconnect_timeout = config->disconnect_timeout;

	/* Initialize mutex */
	if (pthread_mutex_init(&state->mutex, NULL) != 0)
	{
		putErrmsg("mqttcli: can't initialize mutex.", NULL);
		return -1;
	}
	state->hasMutex = 1;

	/* Create MQTT client */
	rc = MQTTClient_create(&state->client, config->broker_address,
				state->clientId, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		putErrmsg("mqttcli: failed to create MQTT client.", NULL);
		pthread_mutex_destroy(&state->mutex);
		state->hasMutex = 0;
		return -1;
	}

	/* Set callbacks */
	rc = MQTTClient_setCallbacks(state->client, state,
				mqtt_connection_lost_callback,
				mqtt_message_arrived_callback,
				mqtt_delivery_complete_callback);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		putErrmsg("mqttcli: failed to set callbacks.", NULL);
		MQTTClient_destroy(&state->client);
		pthread_mutex_destroy(&state->mutex);
		state->hasMutex = 0;
		return -1;
	}

	/* Setup connection options */
	conn_opts.keepAliveInterval = config->keepalive_interval;
	conn_opts.cleansession = 1;
	conn_opts.username = config->username;
	conn_opts.password = config->password;
	conn_opts.connectTimeout = config->connect_timeout / 1000;

	/* Connect to broker */
	writeMemoNote("[i] mqttcli: connecting to broker", config->broker_address);
	rc = MQTTClient_connect(state->client, &conn_opts);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		putErrmsg("mqttcli: failed to connect to broker.", NULL);
		MQTTClient_destroy(&state->client);
		pthread_mutex_destroy(&state->mutex);
		state->hasMutex = 0;
		return -1;
	}

	state->connected = 1;
	state->running = 1;

	writeMemo("[i] mqttcli: connected to MQTT broker");

	/* Subscribe to topic */
	rc = MQTTClient_subscribe(state->client, state->topic, config->qos);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		putErrmsg("mqttcli: failed to subscribe to topic.", state->topic);
		MQTTClient_disconnect(state->client, state->disconnect_timeout);
		MQTTClient_destroy(&state->client);
		pthread_mutex_destroy(&state->mutex);
		state->connected = 0;
		state->hasMutex = 0;
		return -1;
	}

	writeMemoNote("[i] mqttcli: subscribed to topic", state->topic);

	return 0;
}

/* Cleanup MQTT client */
static void mqtt_cleanup(MqttClientState *state)
{
	if (state == NULL)
		return;

	state->running = 0;

	if (state->connected)
	{
		MQTTClient_disconnect(state->client, state->disconnect_timeout);
		state->connected = 0;
	}

	MQTTClient_destroy(&state->client);

	if (state->hasMutex)
	{
		pthread_mutex_destroy(&state->mutex);
		state->hasMutex = 0;
	}
}

/* Main entry point */
#if defined (ION_LWT)
int	mqttcli(saddr a1, saddr a2, saddr a3, saddr a4, saddr a5,
		saddr a6, saddr a7, saddr a8, saddr a9, saddr a10)
{
	char		*ductName = (char *) a1;
#else
int	main(int argc, char **argv)
{
	char		*ductName = argc > 1 ? argv[1] : NULL;
#endif
	VInduct		*vduct;
	PsmAddress	vductElt;
	Sdr		sdr;
	int		result = 0;
	char		topic[MQTT_MAX_TOPIC_LEN];
	MqttConfig	config;

	if (ductName == NULL)
	{
		PUTS("Usage: mqttcli <topic>");
		PUTS("Example: mqttcli ipn/268484608");
		PUTS("         mqttcli 268484608");
		return 1;
	}

	/* Parse topic from duct name */
	if (mqtt_parse_topic_from_duct_name(ductName, topic, sizeof(topic)) < 0)
	{
		putErrmsg("mqttcli: invalid duct name format.", ductName);
		return 1;
	}

	/* Load MQTT configuration from file */
	if (mqtt_load_config(NULL, &config) < 0)
	{
		putErrmsg("mqttcli: can't load MQTT configuration.", NULL);
		return 1;
	}

	if (bp_attach() < 0)
	{
		putErrmsg("mqttcli can't attach to BP.", NULL);
		return 1;
	}

	sdr = getIonsdr();

	/* Find the induct for this MQTT convergence layer */
	findInduct("mqtt", ductName, &vduct, &vductElt);
	if (vduct == NULL)
	{
		putErrmsg("mqttcli: no such induct defined.", ductName);
		bp_detach();
		return 1;
	}

	if (vduct->cliPid != ERROR && vduct->cliPid != sm_TaskIdSelf())
	{
		putErrmsg("mqttcli: induct is already opened.", ductName);
		bp_detach();
		return 1;
	}

	/* Mark induct as opened by this task */
	CHKZERO(sdr_begin_xn(sdr));
	vduct->cliPid = sm_TaskIdSelf();
	sdr_end_xn(sdr);

	/* Store vduct globally for callbacks */
	mqttcli_vduct = vduct;

	/* Set running flag */
	running = 1;

	/* Initialize MQTT subsystem */
	if (mqtt_init(&mqttState, topic, &config) < 0)
	{
		putErrmsg("mqttcli: can't initialize MQTT client.", NULL);
		running = 0;
		CHKZERO(sdr_begin_xn(sdr));
		vduct->cliPid = ERROR;
		sdr_end_xn(sdr);
		mqtt_free_config(&config);
		bp_detach();
		return 1;
	}

	/* Main daemon loop */
	writeMemo("[i] mqttcli daemon running, press CTRL+C to stop.");

	/* The MQTT library handles message reception via callbacks,
	 * so we just need to keep the daemon alive */
	while (running && mqttState.connected)
	{
		snooze(1);

		/* Check if we need to reconnect */
		if (!mqttState.connected && running)
		{
			writeMemo("[i] mqttcli: attempting to reconnect...");
			mqtt_cleanup(&mqttState);
			if (mqtt_init(&mqttState, topic, &config) < 0)
			{
				writeMemo("[!] mqttcli: reconnection failed, will retry...");
				snooze(5);
			}
		}
	}

	/* Mark induct as closed */
	CHKZERO(sdr_begin_xn(sdr));
	vduct->cliPid = ERROR;
	sdr_end_xn(sdr);

	/* Cleanup */
	mqtt_cleanup(&mqttState);
	mqtt_free_config(&config);

	writeErrmsgMemos();
	writeMemo("[i] mqttcli daemon stopping.");

	bp_detach();
	return result;
}
