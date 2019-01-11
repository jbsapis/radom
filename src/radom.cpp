//Importation des librairies
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <String.h>
//Installer les librairies Adafruit Unified Sensors by Adafruit
//et DHT sensor library by Adafruit
#include <Adafruit_Sensor.h>
#include <DHT.h>
//installer la librairie DS3231 by Jean-Claude Wippler
#include <DS3231.h>
#include <Wire.h>
#include <PersonalData.h>
#include <EEPROM.h>

// Liste des fonctions
void sendMessage(String message);
void setConsigne(String message, int indexConsigne);
void heatingProg();
void turnOn() ;
void turnOnWithoutMessage() ;
void turnOff() ;
void turnOffWithoutMessage() ;
String getMeteo() ;
String getDate() ;
void sendStatus() ;
void i2c_eeprom_write_byte( int deviceaddress, unsigned int eeaddress, byte data );
byte i2c_eeprom_read_byte( int deviceaddress, unsigned int eeaddress );
void eepromWriteData(float value);
float eepromReadData();

//Définition des pinouts
#define COMMUN_NON_PRESENT 6
#define DHTPIN 9 //Renseigne la pinouille connectée au DHT
SoftwareSerial gsm(10, 11); // Pins TX,RX du Arduino
#define RELAY 12 // Pin connectée au relai
#define LED 13
//pin 4,5 -> I2C DS3231

//Variables de texte
String textMessage;
String meteoMessage = "";
String consigneKeyWord = "Consigne ";
String newConsigneMessagePrefix = "La nouvelle consigne est de ";

//Variable et constantes pour la gestion du DHT
#define DHTTYPE DHT22 // Remplir avec DHT11 ou DHT22 en fonction
DHT dht(DHTPIN, DHTTYPE);

//Variables pour la gestion du temps
DS3231 Clock;
bool Century=false;
bool h12;
bool PM;

//Variables de mémorisation d'état
int previousState = HIGH; // Etat de présence précédent du secteur commun
bool HeatingOn; // Variable d'état du relais (et du chauffage)
bool enableProg = false; // Programmation active ou non

//Programmation de la consigne de Programmation
#define hysteresis 1.0
float consigne;
float newConsigne = 1.0;

//MODE DEBUG
//Permet d'afficher le mode débug dans la console
//Beaucoup plus d'infos apparaissent
#define DEBUG 0 // 0 pour désactivé et 1 pour activé

//Récupération des données privées qui ne sont pas uploadées dans GITHUB
PersonalData PersonalData; // Objet contenant les données sensibles
String phoneNumber = PersonalData.getPhoneNumber();


/*SETUP************************************************************************/
void setup() {
  // Start the I2C interface
  Wire.begin();
  //Configuration des I/O
  pinMode(RELAY, OUTPUT);
  /* Place la broche du capteur en entrée avec pull-up */
  pinMode(DHTPIN, INPUT_PULLUP);
  pinMode(COMMUN_NON_PRESENT, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  digitalWrite(RELAY, HIGH); // The current state of the relay is Off Passant à l'état repos (connecté en mode normalement fermé NC)
  HeatingOn = false;

  //Récupération de la consigne enregistrée
  consigne = eepromReadSavedConsigne();

  //Demarrage du DHT
  dht.begin();
  //Demarrage Serial
  Serial.begin(9600);
  //Demarrage GSM
  gsm.begin(9600);
  delay(5000);//Attente accrochage réseau
  Serial.println("gsm ready...\r\n");
  gsm.println("AT+CMGF=1\r\n");
  delay(1000);
  gsm.print("AT+CNMI=2,2,0,0,0\r\n");
  delay(1000);
  Serial.println("Ok");

  if(DEBUG) {// Test de la configuration du numéro de téléphone
    Serial.print("Le numéro de téléphone que doit appeler l'Arduino est :");
    Serial.print(phoneNumber);
    Serial.println(".");
  }
  sendStatus(); //Envoie un SMS avec le statut
}

/*LOOP************************************************************************/
void loop() {
  if (gsm.available() > 0) {
    textMessage = gsm.readString();
    if (DEBUG) {
      Serial.print(textMessage);
    }
  }
  if (textMessage.indexOf("+CMT:") > 0 ){ // SMS arrived
    if (textMessage.indexOf("Ron") >= 0) { //If you sent "ON" the lights will turn on
    turnOn();
  } else if (textMessage.indexOf("Roff") >= 0) {
    turnOff();
  } else if (textMessage.indexOf("Status") >= 0) {
    sendStatus();
  } else if (textMessage.indexOf("Progon") >= 0) {
    enableProg = true;
    sendMessage("Programme actif");
    digitalWrite(LED, HIGH);
  } else if (textMessage.indexOf("Progoff") >= 0) {
    enableProg = false;
    sendMessage("Programme inactif");
    digitalWrite(LED, LOW);
    turnOff();
  } else if (textMessage.indexOf(consigneKeyWord) >= 0) { //Mot clé de changement de consigne trouvé dans le SMS
    setConsigne(textMessage, textMessage.indexOf(consigneKeyWord));
  }
  textMessage="";
  delay(100);
  }
  if (enableProg) {
    heatingProg();
  }
  if (!digitalRead(COMMUN_NON_PRESENT) && (previousState == HIGH)) { // si commun present et état précedent non présent
    if (!HeatingOn) {
      digitalWrite(RELAY, LOW); // Relai passant
      previousState = LOW;
      if (DEBUG) {
        Serial.println("Marche forcée secteur commun activée");
      }
    }
  }
  if (digitalRead(COMMUN_NON_PRESENT) && (previousState == LOW)) { // si plus de commun
    if (!HeatingOn) { // et si pas de chauffage en cours
      digitalWrite(RELAY, HIGH); // Relai bloqué
      previousState = HIGH;
      if (DEBUG) {
        Serial.println("Marche forcée secteur commun désactivée");
      }
    }
  }
}

//Envoi du "Message" par sms
void sendMessage(String message) {
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  // RECEIVER: change the phone number here with international code
  delay(500);
  gsm.print(message);
  gsm.write( 0x1a ); //Permet l'envoi du sms
}

//Réglage de la consigne contenue dans le message à l'indexConsigne
void setConsigne(String message, int indexConsigne) {
  newConsigne = message.substring(indexConsigne + consigneKeyWord.length(), message.length()).toFloat(); // On extrait la valeur et on la cast en float
  Serial.print("nouvelle consigne :");
  Serial.println(newConsigne);
  if (!newConsigne) {// Gestion de l'erreur de lecture et remontée du bug
    if (DEBUG) {
      Serial.println("Impossible d'effectuer la conversion de la température String -> Float. Mauvais mot-clé? Mauvais index?");
      Serial.print("indexConsigne = ");
      Serial.println(indexConsigne);
      Serial.print("consigne lenght (>0)= ");
      Serial.println(message.length()- indexConsigne + consigneKeyWord.length());
      Serial.print("newConsigne = ");
      Serial.println(newConsigne);
    } else {
    sendMessage("Erreur de lecture de la consigne envoyee");
    }
  } else if (consigne != newConsigne) { //Si tout se passe bien et la consigne est différente la consigne actuelle
    consigne = newConsigne;
    message = newConsigneMessagePrefix;
    message.concat(consigne);
    sendMessage(message);
    eepromWriteData(consigne);//Enregistrement dans l'EEPROM
    //TODO: activer la programmation
  } else {
    sendMessage("Cette consigne est deja enregistree");
    //TODO: activer la programmation
  }
}

//Vérification de le temp, comparaison avec la consigne,
//et activation/désactivation en fonction
void heatingProg(){
  meteoMessage = getMeteo();
  int index = meteoMessage.indexOf("Temp: ");
  String temp = meteoMessage.substring(index+6, index+11);
  if ((temp.toFloat() < (consigne - 0.5*hysteresis)) && !HeatingOn) {
    turnOnWithoutMessage();
    if (DEBUG) {
      Serial.print(temp);
    }

  }
  if ((temp.toFloat() > (consigne + 0.5*hysteresis)) && HeatingOn) {
    turnOffWithoutMessage();
    if (DEBUG) {
      Serial.print(temp);
    }
  }
  delay(1000);//On ne vérifie la temp que toutes les secondes.
}

//allumage du radiateur si pas de consigne et envoi de SMS
void turnOn() {
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  if (enableProg) {
    gsm.println("Le programme est toujours actif !!");
  } else {
    // Turn on RELAY and save current state
    gsm.println("Chauffage en marche.");
    digitalWrite(RELAY, LOW);
    HeatingOn = true;
  }
  gsm.write( 0x1a ); //Permet l'envoi du sms
}

//allumage du radiateur si pas de consigne
void turnOnWithoutMessage() {
  // Turn on RELAY and save current state
  digitalWrite(RELAY, LOW);
  HeatingOn = true;
}

//Extinction du rad si pas de consigne et envoie de SMS
void turnOff() {
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  if (enableProg) {
    gsm.println("Le programme est toujours actif !!");
  } else {
    // Turn off RELAY and save current state
    gsm.println("Le chauffage est eteint.");
    digitalWrite(RELAY, HIGH);
    HeatingOn = false;
  } //Emet une alerte si le programme est toujours actif
  gsm.write( 0x1a ); //Permet l'envoi du sms
  previousState = HIGH;
}

//Extinction du rad si pas de consigne
void turnOffWithoutMessage() {
  // Turn on RELAY and save current state
  digitalWrite(RELAY, HIGH);
  HeatingOn = false;
  previousState = HIGH;
}

//Renvoie un message météo
String getMeteo() {
  String meteo = "";
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if ((isnan(h) || isnan(t) || isnan(f)) && DEBUG) {
    Serial.println("Failed to read from DHT sensor!");
    return "echec DHT";
  }

  if (DEBUG) {
    Serial.print("Humidite: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    Serial.print(t);
    Serial.println(" *C ");
    Serial.print("Consigne: ");
    Serial.print(consigne);
    Serial.println(" *C ");
  }
  meteo += "Hyg: ";
  meteo += h;
  meteo += " % ";
  meteo += "Temp: ";
  meteo += t;
  meteo += " *C";
  return meteo;
}

//Renvoie la date
String getDate() {
  //Ce code concatène dans "date" la date et l'heure courante
  //dans le format 20YY MM DD HH:MM:SS
  String date ="";
  date +="2";
  date +="0";
  date += String(Clock.getYear());
  date += " ";
  date += String(Clock.getMonth(Century));
  date += " ";
  date += String(Clock.getDate());
  date += " ";
  date += String(Clock.getHour(h12, PM));
  date += ":";
  date += String(Clock.getMinute());
  date += ":";
  date += String(Clock.getSecond());

  //Ce code est exécuté si la variable DEBUG est a TRUE
  //Permet d'afficher dans la console la date et l'heure au format
  // YYYY MM DD w HH MM SS 24h
  if (DEBUG) {
    Serial.print("2");
    if (Century) {      // Won't need this for 89 years.
    Serial.print("1");
  } else {
    Serial.print("0");
  }
  Serial.print(Clock.getYear(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getMonth(Century), DEC);
  Serial.print(' ');
  Serial.print(Clock.getDate(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getDoW(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getHour(h12, PM), DEC);
  Serial.print(' ');
  Serial.print(Clock.getMinute(), DEC);
  Serial.print(' ');
  Serial.print(Clock.getSecond(), DEC);
  if (h12) {
    if (PM) {
      Serial.print(" PM ");
    } else {
      Serial.print(" AM ");
    }
  } else {
    Serial.println(" 24h ");
  }
  return date;
}
}

//Envoie par SMS le statut
void sendStatus() {
  gsm.print("AT+CMGS=\"");
  gsm.print(phoneNumber);
  gsm.println("\"");
  delay(500);
  gsm.print("Le chauffage est actuellement ");
  gsm.println(HeatingOn ? "ON" : "OFF"); // This is to show if the light is currently switched on or off
  gsm.println(getMeteo());
  gsm.println(getDate());
  gsm.print("Consigne: ");
  gsm.println(consigne);
  gsm.write( 0x1a );
}

//Ecrtiture par byte dans l'EEPROM
void i2c_eeprom_write_byte( int deviceaddress, unsigned int eeaddress, byte data ) {
  int rdata = data;
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8)); // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.write(rdata);
  Wire.endTransmission();
}

//Lecture par Byte dans l'EEPROM
byte i2c_eeprom_read_byte( int deviceaddress, unsigned int eeaddress ) {
  byte rdata = 0xFF;
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8)); // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.endTransmission();
  Wire.requestFrom(deviceaddress,1);
  if (Wire.available()) rdata = Wire.read();
  return rdata;
}

//Ecriture de la value dans l'EEPROM
void eepromWriteData(float value) {
  String stringValue = String(value);
  int valueLength = sizeof(stringValue);
  if (valueLength < 32) { //A priori il existe une limite, voir dans AT24C32_examples
    if (DEBUG) {
      Serial.println("**Debug de l'ecriture de l'EEPROM");
      Serial.print("Longueur de la consigne : ");
      Serial.println(valueLength-1);
      Serial.print("Valeur de la consigne : ");
      Serial.println(stringValue);
    }
    for (int i = 0; i < valueLength - 1; i++) { // -1 pour ne pas récupérer le \n de fin de string
      i2c_eeprom_write_byte(0x57, i, stringValue[i]);
      delay(10);
    }
  }
}

//Renvoie la valeur de la consigne lue dans l'EEPROM
float eepromReadSavedConsigne() {
  int b;
  String value;
  for(int i=0;i<5;i++) // la valeur sera "normalement" toujours 5 pour une consigne
  {
    b = i2c_eeprom_read_byte(0x57, i); //access an address from the memory
    value += char(b);
  }
  if (DEBUG) {
    Serial.print("**Read value from EEPROM: ");
    Serial.println(value);
  }
  return value.toFloat();
}