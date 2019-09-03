/*
 * Copyright (c) 2013, Matthias Kovatsch
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"

#define REST_RES_PUSHING 1
#define REST_RES_LEDS 1
#define REST_RES_TEMP 0
#define REST_RES_STATUS 1
#define PLATFORM_HAS_SHT11 1
#define PLATFORM_HAS_LEDS 1

#include "erbium.h"

#if defined (PLATFORM_HAS_LEDS)
#include "dev/leds.h"
#endif
#if defined (PLATFORM_HAS_SHT11)
#include "dev/sht11-sensor.h"
#include "lib/random.h"
#endif

#if WITH_COAP == 3
#include "er-coap-03.h"
#elif WITH_COAP == 7
#include "er-coap-07.h"
#elif WITH_COAP == 12
#include "er-coap-12.h"
#elif WITH_COAP == 13
#include "er-coap-13.h"
#else
#warning "Erbium without CoAP-specifc functionality"
#endif


#define DEBUG 1
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#define PRINT6ADDR(addr) PRINTF("[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]", ((uint8_t *)addr)[0], ((uint8_t *)addr)[1], ((uint8_t *)addr)[2], ((uint8_t *)addr)[3], ((uint8_t *)addr)[4], ((uint8_t *)addr)[5], ((uint8_t *)addr)[6], ((uint8_t *)addr)[7], ((uint8_t *)addr)[8], ((uint8_t *)addr)[9], ((uint8_t *)addr)[10], ((uint8_t *)addr)[11], ((uint8_t *)addr)[12], ((uint8_t *)addr)[13], ((uint8_t *)addr)[14], ((uint8_t *)addr)[15])
#define PRINTLLADDR(lladdr) PRINTF("[%02x:%02x:%02x:%02x:%02x:%02x]",(lladdr)->addr[0], (lladdr)->addr[1], (lladdr)->addr[2], (lladdr)->addr[3],(lladdr)->addr[4], (lladdr)->addr[5])
#else
#define PRINTF(...)
#define PRINT6ADDR(addr)
#define PRINTLLADDR(addr)
#endif

// Limit for the random number generator (used for temperature)
const int rand_max = 20;

// Limit the temperature that the thermostat can sense, for avoiding exceeding short int size
const int max_sensing_temp = 60;
const int min_sensing_temp = 1;

// Structure describing the current status of the thermostat
typedef struct thermostat {
	uint8_t heating;
	uint8_t air_conditioning;
	uint8_t ventilation;
	unsigned short temp;
} t_thermostat;

static t_thermostat thermostat_status;

/******************************************************************************/
/* GET method for requesting the current temperature of the sensor.
   The flag REST_RES_TEMP is set to 0, since the currently used method is the periodic one (COAP observe)
   The returned value is a number. */
#if REST_RES_TEMP
RESOURCE(temperature, METHOD_GET, "temperature", "title=\"Temperature sensor\";rt=\"Data\"");

void
temperature_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{

  /* Code to read the current temperature from the SHT11 sensor.
     Currently the value is random and controlled in the code, since 
     we are doing a simulation of the sensor. */
  //uint16_t tempval = ((sht11_sensor.value(SHT11_SENSOR_TEMP) / 10) - 396) / 10;

  PRINTF("temperature_handler: %u \n", thermostat_status.temp);
  
  // Response header and payload
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, "%u", thermostat_status.temp);
  REST.set_response_payload(response, (uint8_t *)buffer, strlen((char *)buffer));
}
#endif /*REST_RES_TEMP*/

/******************* TEMPERATURE OBSERVE **********************************/
/* This GET method deals with a COAP observe request for observing the value of the temperature in intervals of 5s.
   The COAP request is sent in NodeRed when deployed and the method sends a payload containing the temperature every 5 seconds. */
#if REST_RES_PUSHING
PERIODIC_RESOURCE(tempobs, METHOD_GET, "temperature", "title=\"Temperature observe\";obs", 5*CLOCK_SECOND); //every 5 seconds

void
tempobs_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  PRINTF("tempobs_handler: %u \n", thermostat_status.temp);
  
  // Set response header and payload after the first request (i.e. after the subscribe)
  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, "%u", thermostat_status.temp);
  REST.set_response_payload(response, (uint8_t *)buffer, strlen((char *)buffer));
}

void
tempobs_periodic_handler(resource_t *r)
{
  static uint16_t obs_counter = 0;
  static char content[11];

  ++obs_counter;

  PRINTF("Observe %u for /%s: %u\n", obs_counter, r->url, thermostat_status.temp);

  /* Build notification for the subscribers */
  coap_packet_t notification[1]; /* This way the packet can be treated as pointer as usual. */
  coap_init_message(notification, COAP_TYPE_NON, REST.status.OK, 0 );
  coap_set_payload(notification, content, snprintf(content, sizeof(content), "%u", thermostat_status.temp));

  /* Notify the registered observers with the given message type, observe option, and payload. */
  REST.notify_subscribers(r, obs_counter, notification);
}

#endif /* REST_RES_PUSHING */

/********************** STATUS **************************/
/* Method that returns the status of the thermostat. 
   The method responds with a JSON containing the heating, conditioning and ventilation status.
   The response is used to show on the NodeRed Dashboard the controls that are activated on the thermostat. */
#if REST_RES_STATUS
RESOURCE(status, METHOD_GET, "status", "title=\"Thermostat status\";rt=\"Data\"");

void
status_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  PRINTF("status_handler: heating %u, conditioning %u, ventilation %u\n", thermostat_status.heating, thermostat_status.air_conditioning, thermostat_status.ventilation);
  
  // Response header and payload
  REST.set_header_content_type(response, REST.type.APPLICATION_JSON);
  snprintf((char *)buffer, REST_MAX_CHUNK_SIZE, "[{\"heating\": %u}, {\"conditioning\": %u}, {\"ventilation\": %u}]", 	thermostat_status.heating, thermostat_status.air_conditioning, thermostat_status.ventilation);
  REST.set_response_payload(response, (uint8_t *)buffer, strlen((char *)buffer));
}

#endif /* REST_RES_STATUS */

/******************************************************************************/
#if defined (PLATFORM_HAS_LEDS)
/******************************************************************************/

#if REST_RES_LEDS
/* This resource controls the LEDs of the sensor, by activating or deactivating them according to the received data.
   The request URL contains the color of the requested LED.
   The POST variable mode contains the command (ON or OFF) for the chosen LED.
   Each color corresponds to a variable of the thermostat: (r) heating, (g) ventilation, (b) air conditioning. 
   The method returns an error when the user tries to activate both air conditioning and heating. */
RESOURCE(leds, METHOD_POST , "leds", "title=\"LEDs: ?color=r|g|b, POST mode=on|off\";rt=\"Control\"");

void
leds_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  const char *color = NULL;
  const char *mode = NULL;
  const char *msg = NULL;
  uint8_t led = 0;
  int success = 1;
  uint8_t* unit_type_p = NULL; // pointer to the type of engine (heating, air conditioning, ventilation) that is the object of the request
  
  /* Retrieve the query variable for color */
  size_t query_variable = REST.get_query_variable(request, "color", &color);
  /* Retrieve the query variable for mode on/off */
  size_t post_variable = REST.get_post_variable(request, "mode", &mode);
  
  //Check which kind of engine have to be turn on/off
  if (query_variable) {
    //PRINTF("color %.*s\n", query_variable, color);

    if (strncmp(color, "r", query_variable)==0) { //heating
      led = LEDS_RED;
      unit_type_p = &thermostat_status.heating;  //take the reference of the heating variable
    } else if(strncmp(color,"g", query_variable)==0) { //ventilation
      led = LEDS_GREEN;
      unit_type_p = &thermostat_status.ventilation;  //take the reference of the ventilation variable
    } else if (strncmp(color,"b", query_variable)==0) { //air conditioning
      led = LEDS_BLUE;
      unit_type_p = &thermostat_status.air_conditioning;  //take the reference of the air conditioning variable
    } else {
      success = 0;
    }
  } else {
    success = 0;
  }

  // If the type of engine has been recognized, Check the kind of request (turn on/turn off) 
  if (success && post_variable) {
    //PRINTF("mode %s\n", mode);

    if (strncmp(mode, "on", post_variable)==0) {
      // Turn on the led and the corrisponding engine
      // Check if Mutual exclusion of Heating and Air Conditioning is respected. If not, then error.
      // Heating and Air conditioning cannot run simultaneously
      if ((led == LEDS_RED && thermostat_status.air_conditioning) 
           || (led == LEDS_BLUE && thermostat_status.heating)) {
      	PRINTF("Mutual exclusion violated\n");
        success = 0; 
      } else {
      	// Turn off the led and the corrisponding engine
        leds_on(led);       // turn on the selected led
        msg = "mode=on";    // set the message payload
        *unit_type_p = 1;   // turn the selected engine on
      }
    } else if (strncmp(mode, "off", post_variable)==0) {
      // Turn off the led and the corrisponding engine
      leds_off(led);        // turn on the selected led
      msg = "mode=off";     // set the message payload
      *unit_type_p = 0;     // turn the selected engine on
    } else {
      success = 0;
    }
  } else {
    success = 0;
  }
  
  // Return the response depending on the success value
  if (!success) {
    PRINTF("leds_handler: error\n");
    REST.set_response_status(response, REST.status.NOT_ACCEPTABLE);
    msg = "KO";
    REST.set_response_payload(response, msg, strlen(msg));
  } else if (success) {
    PRINTF("leds_handler: color %.*s mode %s\n", query_variable, color, mode);
    REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
    REST.set_response_payload(response, msg, strlen(msg));
  }
}

#endif
#endif /* PLATFORM_HAS_LEDS */

/******************************************************************************/
PROCESS(thermostat_server_process, "Smart Thermostat Server");
AUTOSTART_PROCESSES(&thermostat_server_process);

PROCESS_THREAD(thermostat_server_process, ev, data)
{
  
  PROCESS_BEGIN();

#ifdef RF_CHANNEL
  PRINTF("RF channel: %u\n", RF_CHANNEL);
#endif
#ifdef IEEE802154_PANID
  PRINTF("PAN ID: 0x%04X\n", IEEE802154_PANID);
#endif

  PRINTF("uIP buffer: %u\n", UIP_BUFSIZE);
  PRINTF("LL header: %u\n", UIP_LLH_LEN);
  PRINTF("IP+UDP header: %u\n", UIP_IPUDPH_LEN);
  PRINTF("REST max chunk: %u\n", REST_MAX_CHUNK_SIZE);

  /* Initialize the REST engine. */
  rest_init_engine();

  /* Activate the application-specific resources. */

/* Resource for retrieving the current temperature */
#if REST_RES_TEMP
  //SENSORS_ACTIVATE(sht11_sensor);
  rest_activate_resource(&resource_temperature);
#endif

/* Resource for retrieving the status of the thermostat */
#if REST_RES_STATUS
  rest_activate_resource(&resource_status);
#endif

/* Resource for observe temperature */
#if REST_RES_PUSHING
  rest_activate_periodic_resource(&periodic_resource_tempobs);
#endif
#if defined (PLATFORM_HAS_LEDS)
#if REST_RES_LEDS
  rest_activate_resource(&resource_leds);
#endif
#endif /* PLATFORM_HAS_LEDS */
  
  /* Thermostat initialization 
     Set all the engine to off and generates a random value 
     for the temperature (between 10 and 30) */
  thermostat_status.heating = 0;
  thermostat_status.air_conditioning = 0;
  thermostat_status.ventilation = 0;
  thermostat_status.temp = (random_rand() % rand_max) + 10;
  
  PRINTF("Random temperature: %u\n", thermostat_status.temp);
  
  // Declaration of the timer
  static struct etimer timer;
  
  /* Thermostat internal logic */
  // Set and start the timer for increasing or decrasing the temperature, if necessary
  etimer_set(&timer, CLOCK_SECOND * 20);
  
  while(1) {
    PROCESS_WAIT_EVENT();
    
    if(ev == PROCESS_EVENT_TIMER){
      unsigned short vent_multiplier = 1;
      // If the ventilation is on, the multiplier is set to 2
      if(thermostat_status.ventilation == 1) {
        vent_multiplier = 2;
      }
      // If the active engine is heating and the maximum limit is not reached, the temperature increases
      if(thermostat_status.heating == 1 && thermostat_status.temp < max_sensing_temp){ // Heating ON
        PRINTF("Temperature increased: +%u\n", vent_multiplier);
        thermostat_status.temp += 1 * vent_multiplier;
      }
      // If the active engine is air conditioning and the minimum limit is not reached yet, the temperature decreases
      if(thermostat_status.air_conditioning == 1 && thermostat_status.temp > min_sensing_temp){ // Air conditioning ON
        PRINTF("Temperature decreased: -%u\n", vent_multiplier);
        thermostat_status.temp -= 1 * vent_multiplier;
      }
      
      // Reset timer
      etimer_reset(&timer);
    }
    
  } /* while (1) */
  
  //SENSOR_DEACTIVATE(sht11_sensor);
  PROCESS_END();
}
