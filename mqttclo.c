/*
	mqttclo.c:	ION MQTT Convergence Layer outduct adapter daemon.
			Handles bundle transmission via MQTT.

	Author: Samo Grasic <samo@grasic.net>
	Based on: ION TCPCL v4 implementation

	Copyright (c) 2026, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
	acknowledged.
									*/

#include "mqttcli.h"
#include "bpP.h"
#include <sys/time.h>
#include <pthread.h>

/* MQTT Outduct State */
typedef struct {
	char		*ductName;		/* Outduct name */
	VOutduct	*vduct;			/* Volatile outduct reference */
	MQTTClient	client;			/* MQTT client handle */
	char		clientId[64];		/* Unique client ID */
	char		topic[MQTT_MAX_TOPIC_LEN]; /* MQTT topic for publishing */
	int		connected;		/* Connection state */
	int		disconnect_timeout;	/* Disconnect timeout (ms) */
	int		qos;			/* QoS level */
	MqttConfig	config;			/* MQTT configuration */
	pthread_t	senderThread;		/* Bundle transmission thread */
	int		hasSenderThread;	/* Boolean */
	pthread_mutex_t	mutex;			/* State protection */
	int		hasMutex;		/* Boolean */
	int		running;		/* Boolean */
	time_t		lastActivity;		/* For keepalive monitoring */
} MqttOutductState;

/* Forward declarations */
static int	mqttclo_init(MqttOutductState *state, const char *topic, MqttConfig *config);
static void	mqttclo_cleanup(MqttOutductState *state);
static void*	mqttclo_sender_thread(void *arg);
static int	mqttclo_send_bundle(MqttOutductState *state, Object bundleZco,
			BpAncillaryData *ancillaryData);

/* MQTT callback: Connection lost */
static void mqttclo_connection_lost_callback(void *context, char *cause)
{
	writeMemoNote("[!] mqttclo: connection lost", cause ? cause : "unknown reason");

	MqttOutductState *state = (MqttOutductState *)context;
	if (state)
	{
		pthread_mutex_lock(&state->mutex);
		state->connected = 0;
		pthread_mutex_unlock(&state->mutex);
	}
}

/* MQTT callback: Message arrived (not used for outduct) */
static int mqttclo_message_arrived_callback(void *context, char *topicName,
		int topicLen, MQTTClient_message *message)
{
	/* Not used for outduct */
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	return 1;
}

/* MQTT callback: Delivery complete */
static void mqttclo_delivery_complete_callback(void *context,
		MQTTClient_deliveryToken dt)
{
	writeMemoNote("[i] mqttclo: message delivery confirmed, token", itoa((int)dt));
}

/* Initialize MQTT outduct */
static int mqttclo_init(MqttOutductState *state, const char *topic, MqttConfig *config)
{
	MQTTClient_connectOptions	conn_opts = MQTTClient_connectOptions_initializer;
	int				rc;
	char				client_id_prefix[128];

	/* Build client ID prefix */
	snprintf(client_id_prefix, sizeof(client_id_prefix), "%sclo_", config->client_id_prefix);

	/* Generate unique client ID */
	mqtt_generate_client_id(state->clientId, sizeof(state->clientId), client_id_prefix);

	/* Store topic */
	if (strlen(topic) >= MQTT_MAX_TOPIC_LEN)
	{
		putErrmsg("mqttclo: topic name too long.", topic);
		return -1;
	}
	strcpy(state->topic, topic);

	/* Store config and parameters */
	memcpy(&state->config, config, sizeof(MqttConfig));
	state->disconnect_timeout = config->disconnect_timeout;
	state->qos = config->qos;

	/* Create MQTT client */
	rc = MQTTClient_create(&state->client, config->broker_address,
				state->clientId, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		putErrmsg("mqttclo: failed to create MQTT client.", NULL);
		return -1;
	}

	/* Set callbacks */
	rc = MQTTClient_setCallbacks(state->client, state,
				mqttclo_connection_lost_callback,
				mqttclo_message_arrived_callback,
				mqttclo_delivery_complete_callback);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		putErrmsg("mqttclo: failed to set callbacks.", NULL);
		MQTTClient_destroy(&state->client);
		return -1;
	}

	/* Setup connection options */
	conn_opts.keepAliveInterval = config->keepalive_interval;
	conn_opts.cleansession = 1;
	conn_opts.username = config->username;
	conn_opts.password = config->password;
	conn_opts.connectTimeout = config->connect_timeout / 1000;

	/* Connect to broker */
	writeMemoNote("[i] mqttclo: connecting to broker", config->broker_address);
	rc = MQTTClient_connect(state->client, &conn_opts);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		char errbuf[128];
		snprintf(errbuf, sizeof(errbuf), "failed to connect (rc=%d)", rc);
		putErrmsg("mqttclo: failed to connect to broker.", errbuf);
		MQTTClient_destroy(&state->client);
		return -1;
	}

	state->connected = 1;
	state->running = 1;
	state->lastActivity = time(NULL);

	writeMemo("[i] mqttclo: connected to MQTT broker");
	writeMemoNote("[i] mqttclo: will publish to topic", state->topic);

	return 0;
}

/* Cleanup MQTT outduct */
static void mqttclo_cleanup(MqttOutductState *state)
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

/* Bundle transmission thread */
static void* mqttclo_sender_thread(void *arg)
{
	MqttOutductState	*state = (MqttOutductState *) arg;
	Sdr			sdr;
	Object			bundleZco;
	BpAncillaryData		ancillaryData;
	int			stewardship = 0;

	writeMemoNote("[i] mqttclo sender thread starting for", state->ductName);

	sdr = getIonsdr();

	/* Main transmission loop */
	while (state->running && state->connected)
	{
		/* Check for bundle to send */
		if (bpDequeue(state->vduct, &bundleZco, &ancillaryData, stewardship) < 0)
		{
			putErrmsg("mqttclo: bundle dequeue failed.", NULL);
			break;
		}

		if (bundleZco == 0)  /* No bundle available */
		{
			/* Brief pause before checking again */
			snooze(1);
			continue;
		}

		/* Send the bundle */
		if (mqttclo_send_bundle(state, bundleZco, &ancillaryData) < 0)
		{
			putErrmsg("mqttclo: bundle transmission failed.", NULL);

			/* Return bundle to limbo */
			CHKZERO(sdr_begin_xn(sdr));
			bpHandleXmitFailure(bundleZco);
			sdr_end_xn(sdr);

			/* Try to reconnect */
			if (!state->connected)
			{
				writeMemo("[i] mqttclo: attempting to reconnect...");
				mqttclo_cleanup(state);
				if (mqttclo_init(state, state->topic, &state->config) < 0)
				{
					writeMemo("[!] mqttclo: reconnection failed");
					break;
				}
			}
			continue;
		}

		state->lastActivity = time(NULL);
	}

	writeMemoNote("[i] mqttclo sender thread stopping for", state->ductName);
	return NULL;
}

/* Send a bundle via MQTT */
static int mqttclo_send_bundle(MqttOutductState *state, Object bundleZco,
		BpAncillaryData *ancillaryData)
{
	Sdr			sdr = getIonsdr();
	ZcoReader		reader;
	vast			bundleLength;
	unsigned char		*buffer;
	vast			bytesRead;
	MQTTClient_message	pubmsg = MQTTClient_message_initializer;
	MQTTClient_deliveryToken token;
	int			rc;

	/* Get bundle length */
	CHKZERO(sdr_begin_xn(sdr));
	bundleLength = zco_length(sdr, bundleZco);
	sdr_end_xn(sdr);

	if (bundleLength <= 0)
	{
		putErrmsg("mqttclo: invalid bundle length.", NULL);
		return -1;
	}

	if (bundleLength > MQTT_MAX_BUNDLE_SIZE)
	{
		putErrmsg("mqttclo: bundle too large for MQTT.", NULL);
		return -1;
	}

	writeMemoNote("[i] mqttclo: sending bundle of length", itoa((int)bundleLength));

	/* Allocate buffer for bundle data */
	buffer = (unsigned char *) MTAKE((size_t)bundleLength);
	if (buffer == NULL)
	{
		putErrmsg("mqttclo: can't allocate bundle buffer.", NULL);
		return -1;
	}

	/* Read bundle data from ZCO */
	CHKZERO(sdr_begin_xn(sdr));
	zco_start_transmitting(bundleZco, &reader);
	bytesRead = zco_transmit(sdr, &reader, bundleLength, (char *)buffer);
	sdr_end_xn(sdr);

	if (bytesRead != bundleLength)
	{
		putErrmsg("mqttclo: bundle read error.", NULL);
		MRELEASE(buffer);
		return -1;
	}

	/* Prepare MQTT message */
	pubmsg.payload = buffer;
	pubmsg.payloadlen = (int)bundleLength;
	pubmsg.qos = state->qos;
	pubmsg.retained = 0;

	/* Publish to MQTT topic */
	writeMemoNote("[i] mqttclo: publishing to topic", state->topic);

	pthread_mutex_lock(&state->mutex);
	rc = MQTTClient_publishMessage(state->client, state->topic, &pubmsg, &token);
	pthread_mutex_unlock(&state->mutex);

	if (rc != MQTTCLIENT_SUCCESS)
	{
		char errbuf[128];
		snprintf(errbuf, sizeof(errbuf), "publish failed (rc=%d)", rc);
		putErrmsg("mqttclo: failed to publish bundle.", errbuf);
		MRELEASE(buffer);
		return -1;
	}

	writeMemoNote("[i] mqttclo: waiting for delivery confirmation, token", itoa((int)token));

	/* Wait for delivery confirmation */
	rc = MQTTClient_waitForCompletion(state->client, token, state->disconnect_timeout);
	if (rc != MQTTCLIENT_SUCCESS)
	{
		char errbuf[128];
		snprintf(errbuf, sizeof(errbuf), "delivery confirmation failed (rc=%d)", rc);
		putErrmsg("mqttclo: bundle delivery not confirmed.", errbuf);
		MRELEASE(buffer);
		return -1;
	}

	writeMemo("[i] mqttclo: bundle sent successfully");

	/* Handle transmission success */
	CHKZERO(sdr_begin_xn(sdr));
	if (bpHandleXmitSuccess(bundleZco) < 0)
	{
		putErrmsg("mqttclo: can't handle xmit success", NULL);
		sdr_end_xn(sdr);
		MRELEASE(buffer);
		return -1;
	}
	sdr_end_xn(sdr);

	MRELEASE(buffer);
	return 0;
}

/* Main entry point */
#if defined (ION_LWT)
int	mqttclo(saddr a1, saddr a2, saddr a3, saddr a4, saddr a5,
		saddr a6, saddr a7, saddr a8, saddr a9, saddr a10)
{
	char	*ductName = (char *) a1;
#else
int	main(int argc, char **argv)
{
	char	*ductName = argc > 1 ? argv[1] : NULL;
#endif
	MqttOutductState	state;
	VOutduct		*vduct;
	PsmAddress		ductElt;
	Sdr			sdr;
	char			topic[MQTT_MAX_TOPIC_LEN];
	MqttConfig		config;
	int			result = 0;

	if (ductName == NULL)
	{
		PUTS("Usage: mqttclo <topic>");
		PUTS("Example: mqttclo ipn/268484600");
		PUTS("         mqttclo 268484600");
		return 1;
	}

	/* Parse topic from duct name */
	if (mqtt_parse_topic_from_duct_name(ductName, topic, sizeof(topic)) < 0)
	{
		putErrmsg("mqttclo: invalid duct name format.", ductName);
		return 1;
	}

	/* Load MQTT configuration from file */
	if (mqtt_load_config(NULL, &config) < 0)
	{
		putErrmsg("mqttclo: can't load MQTT configuration.", NULL);
		return 1;
	}

	if (bp_attach() < 0)
	{
		putErrmsg("mqttclo can't attach to BP.", NULL);
		return 1;
	}

	/* Find the outduct */
	findOutduct("mqtt", ductName, &vduct, &ductElt);
	if (vduct == NULL)
	{
		putErrmsg("mqttclo: no such outduct.", ductName);
		bp_detach();
		return 1;
	}

	if (vduct->cloPid != ERROR && vduct->cloPid != sm_TaskIdSelf())
	{
		putErrmsg("mqttclo: outduct is already open.", ductName);
		bp_detach();
		return 1;
	}

	/* Initialize outduct state */
	memset(&state, 0, sizeof(state));
	state.ductName = ductName;
	state.vduct = vduct;

	if (pthread_mutex_init(&state.mutex, NULL) != 0)
	{
		putErrmsg("mqttclo: can't initialize mutex.", NULL);
		bp_detach();
		return 1;
	}
	state.hasMutex = 1;

	sdr = getIonsdr();

	/* Mark outduct as opened by this task */
	CHKZERO(sdr_begin_xn(sdr));
	vduct->cloPid = sm_TaskIdSelf();
	sdr_end_xn(sdr);

	/* Initialize MQTT client */
	if (mqttclo_init(&state, topic, &config) < 0)
	{
		putErrmsg("mqttclo: can't initialize MQTT client.", NULL);
		mqtt_free_config(&config);
		result = -1;
	}
	else
	{
		/* Start sender thread */
		if (pthread_create(&state.senderThread, NULL,
				mqttclo_sender_thread, &state) != 0)
		{
			putErrmsg("mqttclo: can't create sender thread.", NULL);
			result = -1;
		}
		else
		{
			state.hasSenderThread = 1;

			/* Wait for sender thread to complete */
			pthread_join(state.senderThread, NULL);
		}
	}

	/* Cleanup */
	mqttclo_cleanup(&state);
	mqtt_free_config(&config);

	/* Mark outduct as closed */
	CHKZERO(sdr_begin_xn(sdr));
	vduct->cloPid = ERROR;
	sdr_end_xn(sdr);

	writeErrmsgMemos();
	writeMemoNote("[i] mqttclo outduct ended", ductName);

	bp_detach();
	return result;
}
