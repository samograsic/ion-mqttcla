/*
	mqttcli.h:	Private definitions for ION MQTT Convergence Layer
			implementation.

	Author: Samo Grasic <samo@grasic.net>
	Based on: ION TCPCL v4 implementation

	Copyright (c) 2026, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
	acknowledged.
									*/

#ifndef _MQTTCLI_H_
#define _MQTTCLI_H_

#include "bpP.h"
#include <stdint.h>
#include <MQTTClient.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MQTT Configuration Structure */
typedef struct {
	char	broker_address[256];		/* Broker URL (tcp://host:port) */
	char	username[128];			/* MQTT username */
	char	password[128];			/* MQTT password */
	char	client_id_prefix[64];		/* Client ID prefix */
	int	qos;				/* QoS level (0, 1, or 2) */
	int	keepalive_interval;		/* Keepalive interval in seconds */
	int	connect_timeout;		/* Connection timeout in milliseconds */
	int	disconnect_timeout;		/* Disconnect timeout in milliseconds */
} MqttConfig;

/* Buffer sizes */
#define MQTT_MAX_TOPIC_LEN		256
#define MQTT_MAX_BUNDLE_SIZE		(10 * 1024 * 1024)	/* 10 MB */
#define MQTT_RECEIVE_BUFFER_SIZE	(512 * 1024)		/* 512 KB */

/* MQTT Client State */
typedef struct {
	MQTTClient	client;			/* Paho MQTT client handle */
	char		clientId[64];		/* Unique client ID */
	char		topic[MQTT_MAX_TOPIC_LEN]; /* MQTT topic for pub/sub */
	int		connected;		/* Connection state */
	int		disconnect_timeout;	/* Disconnect timeout (ms) */
	pthread_mutex_t	mutex;			/* State protection */
	int		hasMutex;		/* Boolean */
	int		running;		/* Boolean */
} MqttClientState;

/* Function prototypes */

/* MQTT connection management */
extern int	mqtt_connect(MqttClientState *state, const char *broker,
			const char *clientId, const char *username,
			const char *password);
extern void	mqtt_disconnect(MqttClientState *state);
extern int	mqtt_subscribe(MqttClientState *state, const char *topic);
extern int	mqtt_publish(MqttClientState *state, const char *topic,
			const void *payload, size_t payload_len);

/* MQTT callbacks */
extern void	mqtt_connection_lost_callback(void *context, char *cause);
extern int	mqtt_message_arrived_callback(void *context, char *topicName,
			int topicLen, MQTTClient_message *message);
extern void	mqtt_delivery_complete_callback(void *context,
			MQTTClient_deliveryToken dt);

/* Configuration functions */
extern int	mqtt_load_config(const char *config_file, MqttConfig *config);
extern void	mqtt_free_config(MqttConfig *config);
extern void	mqtt_print_config(MqttConfig *config);

/* Utility functions */
extern int	mqtt_parse_topic_from_duct_name(const char *ductName,
			char *topic, size_t topic_size);
extern void	mqtt_generate_client_id(char *clientId, size_t size,
			const char *prefix);

#ifdef __cplusplus
}
#endif

#endif /* _MQTTCLI_H_ */
