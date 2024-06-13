/*
 *  main.cpp
 *  Sensible LoRa Remote Switcher - Combined Transmitter and Receiver
 *
 * This code initializes the LoRa module and the switch in the setup() function.
 * If the device is configured as a transmitter, it reads the state of the dry contact and transmit it to the specified receiver.
 * If the device is configured as a receiver, it listens for incoming LoRa packets and toggles the relay accordingly.
 * The code includes a heartbeat, ackknowledgement and timeout mechanism to ensure the reliability of the communication.
 */

// TODO: Decide on MQTT, will it be writeable on TX and RX?
// TODO: Decide on MQTT, will it be a mode switch that disables to digital input
// TODO: Or should there be an option for an OR gate to combine the digital input and MQTT input

#include "functions.h"

// The setup function runs once when you press reset or power the board
void setup()
{
    // Begin serial communication at 115200 baud
    Serial.begin(115200);
    // Wait for serial port to connect
    while (!Serial)
        ;
    // Wait

    if (isTransmitter)
    {
        // TODO: Write this to EEPROM and provide web interface to read it on TX and write it on RX
        // Generate they encryption key
        // generateKeyFromChipId();
        // Set the dry contact pin as an input
        pinMode(INP1, INPUT);
    }

    pinMode(2, OUTPUT); // Set the LED pin as an output
    pinMode(RLY1, OUTPUT); // Set the relay pin as an output - on tx only if local echo is enabled
    LoRa.setPins(nss, rst, dio0); // Set the LoRa module pins
    // Wait
    delay(1000);

    // Initialize the LoRa module at 433 MHz
    LoRa.begin(433E6) ? Serial.println("LoRa started successfully!") : Serial.println("LoRa startup failed!");

    if (enableWiFi)
    {
        setupWiFi();
        setupOta();
        setupMdns();
        mqttClient.setServer(mqtt_server, mqtt_port);
        mqttClient.setCallback(mqttCallback);
    }

    // Print the encryption key
    printKey();

    // Announce startup
    isTransmitter ? Serial.println("TX - Transmitter started!") : Serial.println("RX - Receiver started!");
}

// This is the main loop
void loop()
{
    if (enableWiFi)
    {
        // handle OTA update requests
        ArduinoOTA.handle();
        // handle MQTT messages
        handleMqtt();
    }

    // Handle the transmitter or receiver
    handleTransceiver();

    // Blink the LED
    updateLED();
}