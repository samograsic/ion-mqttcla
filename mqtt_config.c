/*
	mqtt_config.c:	Configuration file parser for MQTT convergence layer

	Author: Samo Grasic <samo@grasic.net>
	Copyright (c) 2026, California Institute of Technology.
	ALL RIGHTS RESERVED.  U.S. Government Sponsorship acknowledged.
*/

#include "mqttcli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Default configuration file locations (checked in order) */
static const char *default_config_paths[] = {
	"./mqtt.conf",
	"/usr/local/etc/ion/mqtt.conf",
	"/etc/ion/mqtt.conf",
	NULL
};

/* Trim whitespace from string */
static char* trim_whitespace(char *str)
{
	char *end;

	/* Trim leading space */
	while (isspace((unsigned char)*str)) str++;

	if (*str == 0)  /* All spaces */
		return str;

	/* Trim trailing space */
	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) end--;

	/* Write new null terminator */
	end[1] = '\0';

	return str;
}

/* Load MQTT configuration from file */
int mqtt_load_config(const char *config_file, MqttConfig *config)
{
	FILE *fp;
	char line[512];
	char *key, *value;
	char *equals;
	int found_broker = 0;
	const char **path;

	if (config == NULL)
	{
		putErrmsg("mqtt_load_config: null config pointer", NULL);
		return -1;
	}

	/* Initialize with defaults */
	memset(config, 0, sizeof(MqttConfig));
	config->qos = 1;
	config->keepalive_interval = 20;
	config->connect_timeout = 10000;
	config->disconnect_timeout = 10000;
	strncpy(config->client_id_prefix, "ion_dtn_", sizeof(config->client_id_prefix) - 1);

	/* If config_file specified, try that first */
	if (config_file != NULL)
	{
		fp = fopen(config_file, "r");
		if (fp == NULL)
		{
			putErrmsg("mqtt_load_config: can't open config file", (char *)config_file);
			return -1;
		}
	}
	else
	{
		/* Try default locations */
		fp = NULL;
		for (path = default_config_paths; *path != NULL; path++)
		{
			fp = fopen(*path, "r");
			if (fp != NULL)
			{
				writeMemoNote("[i] mqtt: loaded config from", (char *)*path);
				break;
			}
		}

		if (fp == NULL)
		{
			putErrmsg("mqtt_load_config: no config file found in default locations", NULL);
			return -1;
		}
	}

	/* Parse configuration file */
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		/* Trim whitespace */
		key = trim_whitespace(line);

		/* Skip empty lines and comments */
		if (key[0] == '\0' || key[0] == '#')
			continue;

		/* Find equals sign */
		equals = strchr(key, '=');
		if (equals == NULL)
			continue;  /* Invalid line, skip */

		/* Split into key and value */
		*equals = '\0';
		value = equals + 1;

		/* Trim key and value */
		key = trim_whitespace(key);
		value = trim_whitespace(value);

		/* Parse known keys */
		if (strcmp(key, "broker_address") == 0)
		{
			strncpy(config->broker_address, value, sizeof(config->broker_address) - 1);
			found_broker = 1;
		}
		else if (strcmp(key, "username") == 0)
		{
			strncpy(config->username, value, sizeof(config->username) - 1);
		}
		else if (strcmp(key, "password") == 0)
		{
			strncpy(config->password, value, sizeof(config->password) - 1);
		}
		else if (strcmp(key, "qos") == 0)
		{
			config->qos = atoi(value);
			if (config->qos < 0 || config->qos > 2)
			{
				writeMemoNote("[w] mqtt: invalid QoS value, using default", value);
				config->qos = 1;
			}
		}
		else if (strcmp(key, "keepalive_interval") == 0)
		{
			config->keepalive_interval = atoi(value);
		}
		else if (strcmp(key, "connect_timeout") == 0)
		{
			config->connect_timeout = atoi(value);
		}
		else if (strcmp(key, "disconnect_timeout") == 0)
		{
			config->disconnect_timeout = atoi(value);
		}
		else if (strcmp(key, "client_id_prefix") == 0)
		{
			strncpy(config->client_id_prefix, value, sizeof(config->client_id_prefix) - 1);
		}
	}

	fclose(fp);

	/* Validate required fields */
	if (!found_broker || strlen(config->broker_address) == 0)
	{
		putErrmsg("mqtt_load_config: broker_address not specified", NULL);
		return -1;
	}

	return 0;
}

/* Free configuration resources */
void mqtt_free_config(MqttConfig *config)
{
	if (config != NULL)
	{
		/* Clear sensitive data (password) */
		memset(config->password, 0, sizeof(config->password));
	}
}

/* Print configuration (for debugging, passwords masked) */
void mqtt_print_config(MqttConfig *config)
{
	if (config == NULL)
		return;

	writeMemo("[i] MQTT Configuration:");
	writeMemoNote("  broker_address", config->broker_address);
	writeMemoNote("  username", config->username);
	writeMemo("  password: ********");
	writeMemoNote("  qos", itoa(config->qos));
	writeMemoNote("  keepalive_interval", itoa(config->keepalive_interval));
	writeMemoNote("  connect_timeout", itoa(config->connect_timeout));
	writeMemoNote("  disconnect_timeout", itoa(config->disconnect_timeout));
	writeMemoNote("  client_id_prefix", config->client_id_prefix);
}
