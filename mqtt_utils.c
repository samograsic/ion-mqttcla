/*
	mqtt_utils.c:	Utility functions for ION MQTT Convergence Layer

	Author: Samo Grasic <samo@grasic.net>

	Copyright (c) 2026, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship
	acknowledged.
									*/

#include "mqttcli.h"
#include <sys/time.h>
#include <unistd.h>

/* Generate unique client ID */
void mqtt_generate_client_id(char *clientId, size_t size, const char *prefix)
{
	struct timeval	tv;
	unsigned int	rand_val;

	gettimeofday(&tv, NULL);
	rand_val = (unsigned int)(tv.tv_sec ^ tv.tv_usec ^ getpid());

	snprintf(clientId, size, "%s%u", prefix, rand_val);
}

/* Parse topic from duct name */
int mqtt_parse_topic_from_duct_name(const char *ductName, char *topic,
		size_t topic_size)
{
	if (ductName == NULL || topic == NULL)
	{
		return -1;
	}

	/* The duct name can be just the topic (e.g., "ipn/268484608")
	 * or include broker info (e.g., "mqtt.openipn.org:1883/ipn/268484608") */

	const char *slash = strchr(ductName, '/');
	if (slash != NULL)
	{
		/* Found a slash, extract everything after the first slash */
		if (strlen(slash) >= topic_size)
		{
			return -1;
		}
		strcpy(topic, slash + 1);  /* Skip the first slash */

		/* Prepend "ipn/" if not already present */
		if (strncmp(topic, "ipn/", 4) != 0)
		{
			char temp[MQTT_MAX_TOPIC_LEN];
			strcpy(temp, topic);
			snprintf(topic, topic_size, "ipn/%s", temp);
		}
	}
	else
	{
		/* No slash, assume it's just a node number */
		snprintf(topic, topic_size, "ipn/%s", ductName);
	}

	return 0;
}
