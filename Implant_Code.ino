#include <SoftwareSerial.h>

//GPS PARSING VARIABLES
int rxPin = 1; //gps serial rx pin
int txPin = 0; //gps serial tx pin
int powerGPS = 4; //gps power pin //CHECK IMPLANT PCB AND FIX ACCORDINGLY
int byteGPS = -1; //used for storing an incoming gps byte
char NMEA_code[7] = "$GPRMC"; //used for specifying gps output to be parsed
boolean code_match = true; //will determine if NMEA_code is matched
boolean ping_successful = false; //will determine when gps data has successfully been obtained
char buffer[300]; //used for storing incoming gps bytes
int buffer_length = 0; //gives the length of buffer being used
int indices[13]; //used for storing the indices of the buffer that correspond to a ',' or '*' character
int indices_length = 0; //gives the length of indices being used

//TIME MONITORING VARIABLES
boolean StartUp = true; //shows that the implant has just been started (or restarted)
int theTime = 0; //keeps track of the hr of the day in min (1 hr = 1,440 min)
unsigned long duration = 0; //keeps track of the # of min passed since theTime was last updated
unsigned long prev_millis = 0; //stores the millisecond count from when theTime was last updated
unsigned long curr_millis = 0; //stores the current millisecond count while updating theTime

//TRANSCEIVER INTERFACE & BASE STATION COMMUNICATION VARIABLES
SoftwareSerial transSerial_R(12, 13); //transceiver serial receive, rx is pin 12 (pin 13 is dummy tx)
SoftwareSerial transSerial_T(13, 12); //transceiver serial transmit, tx is pin 12 (pin 13 is dummy rx)
int TR_Pin = 10; //transceiver transmit/receive select pin (HIGH = transmit, LOW = receive)
int PDN_Pin = 9; //transceiver power down pin (HIGH = power on, LOW = power down) this pin should always be HIGH
boolean transmit_successful = false;
char byteIn = 0; //used for storing an incoming transceiver byte
char base_signal = 63; //detection byte that the base station is broadcasting (decimal 63 equals a '?' character)
char implant_response = 33; //response byte that the base station listens for (decimal 33 equals a '!' character)

//DATA STORAGE & MESSAGE STRUCTURE VARIABLES
const int max_length = 100;
char lat[max_length]; //buffer for storing latitude values
char lon[max_length]; //buffer for storing longitude values
char time[max_length]; //buffer for storing time-stamp values
char date[max_length]; //buffer for storing date-stamp values
int lat_length = 0; //gives the length of lat being used
int lon_length = 0; //gives the length of lon being used
int time_length = 0; //gives the length of time being used
int date_length = 0; //gives the length of date being used
char byteStart = 1; //the start byte is a "start of heading" character
char byteEnd = 13; //the end byte is a "new line" character
char byteStop = 4; //the stop byte is a "end of transmission" character
char end_of_instance = 10; //used to separate instances of gps data (decimal 13 = 'carriage return')
//MESSAGE FORMAT: byteStart, latitudes, byteEnd, longitudes, byteEnd, times, byteEnd, dates, byteEnd, byteStop, byteStart       



void setup(){
  
  pinMode(rxPin, INPUT);
  pinMode(txPin, OUTPUT);
  pinMode(TR_Pin, OUTPUT);
  pinMode(PDN_Pin, OUTPUT);
  digitalWrite(PDN_Pin, HIGH); //power up transceiver
  digitalWrite(TR_Pin, LOW);
  pinMode(12,INPUT);
  Serial.begin(9600);
  transSerial_R.begin(9600);
  transSerial_T.begin(9600);
  //initialize all buffers to have null values (decimal 0 = null character)
  for (int i=0; i<300; i++){
    buffer[i] = 0;
  }
  for(int i=0; i<max_length; i++) {
    lat[i] = 0;
    lon[i] = 0;
    time[i] = 0;
    date[i] = 0;
  }
  
  Serial.println("Setup Complete");
  
} //end "setup"



void loop(){
  
  
  
  //GPS PARSING CODE
  //if the implant is just starting, the time needs to be determined (via gps)
  //if theTime is 8pm, 12am, or 4am, gps data needs to be logged //CHECK WITH GROUP AND CHANGE AS NECESSARY
  if (StartUp == true || (theTime == 790 && ping_successful == false) || (theTime == 0 && ping_successful == false) || (theTime == 240 && ping_successful == false)){
    ping_successful = false; //show that correct gps data has not yet been successfully received
    //digitalWrite(powerGPS, ); //power up gps
    prev_millis = millis(); //used for seeing how long it takes to successfully ping the satellites

    if (StartUp == true){
      Serial.println("'StartUp' Mode: Attempting contact with GPS satellites for time-of-day initialization");
    }
    else{
      Serial.println("'Ping' Mode: Attempting contact with GPS satellites to obtain location data");
    }
    Serial.println("");
    Serial.println("Waiting for valid GPS data...");

    while (ping_successful != true){
      //check how much time has passed since beginning this loop for pinging the satellites
      curr_millis = millis();
      if (prev_millis > curr_millis){ //check for millis() overflow and adjust if necessary
        //millis() has range of 0 to 4294967295, so it overflows after reaching 4294967295
        //overall, duration in minutes needs to equal ((4294967295 + 1) - (prev_millis - curr_millis))/60000
        //because of overflow issues, the above computation needs to be carried out in the following order of steps:
        duration = 4294967295;
        duration -= (prev_millis - curr_millis);
        duration += 1;
        duration /= 60000;
      }
      else{
        duration = (curr_millis - prev_millis)/60000; //duration equals the # of min since last time calculation
      }
      
      //if implant is no longer in start-up mode and duration is greater than 15 min,
      //then the implant is in bad location for acquiring satellite signals, so this attempt should be terminated
      if (StartUp == false && duration > 15){
        Serial.println("");
        Serial.println("Satellite communication cannot be established. Quitting attempt.");
        break; //this will exit the while loop and skip down to the line of code that powers down the gps
      }
      //if the implant is in start-up mode, the duration to successfully acquire gps data may be > than 20 min
      //thus the while loop should continue until valid gps data has been obtained
      
      byteGPS=Serial.read(); //read a byte from the gps serial port
      if (byteGPS == -1){ //check if the serial port is empty
        delay(100); //if so, wait 0.1 seconds
      } 
      else{
        Serial.print(char(byteGPS));
        buffer[buffer_length] = byteGPS; //if there is serial port data, it is put in the buffer
        buffer_length++;                      
        if (byteGPS == 13){ //if the received byte is equal to 13, there is no more data to be received
          //buffer is now ready for parsing
          code_match = true;
          //Serial.println("");
          
          for (int i=1; i<7; i++){ //verify if the received command starts with $GPRMC
            if (buffer[i] != NMEA_code[i-1]){
              code_match = false; //if there is a mismatch, code_match is false
            }
          }
          
          indices_length = 0;
          if (code_match == true){ //if the NMEA_code is matched, continue parsing data
            //need to fill in the indices array for efficient data parsing
            for (int i=0; i<300; i++){
              if (buffer[i]==','){ //check for the position of the  "," separator
                indices[indices_length]=i;
                indices_length++;
              }
              if (buffer[i]=='*'){ //check for the position of the "*" character
                indices[12]=i;
                indices_length++;
              }
            }
            //indices array now updated
            //Serial.println("No reception yet. GPS Status is V");
            if (buffer[indices[1]+1] == 'A'){ //check if the "status" is 'A' OK. If so, then store info into buffer
              Serial.println("");
              Serial.println("");
              Serial.print("Total Wait Time (in Minutes): ");
              Serial.println(duration);
              Serial.println("");
              Serial.println("GPS Status is 'A': Valid GPS data obtained");
              Serial.println("");
              for (int i=0; i<12; i++){
                //CASE 0 : Time in UTC (HhMmSs)
                //CASE 1 : Status (A=OK,V=KO)
                //CASE 2 : Latitude
                //CASE 3 : Direction (N/S)
                //CASE 4 : Longitude
                //CASE 5 : Direction (E/W)
                //CASE 6 : Velocity in knots
                //CASE 7 : Heading in degrees
                //CASE 8 : Date UTC (DdMmAa)
                //CASE 9 : Magnetic degrees
                //CASE 10 : (E/W)
                //CASE 11 : Mode
                //CASE 12 : Checksum
                
                //store time data
                if (i == 0 && time_length < max_length){
                  Serial.print("Time Data: ");
                  for (int j=(indices[i]+1); j<(indices[i]+3); j++){
                    Serial.print(char(buffer[j]));
                    time[time_length] = buffer[j];
                    time_length++;
                  }
                  time[time_length] = ':';
                  time_length++;
                  for (int j=(indices[i]+3); j<(indices[i]+5); j++){
                    Serial.print(char(buffer[j]));
                    time[time_length] = buffer[j];
                    time_length++;
                  }
                  for (int j=(indices[i]+5); j<indices[i+1]; j++){
                    Serial.print(char(buffer[j]));
                  }
                  time[time_length] = end_of_instance;
                  time_length++;
                  
                  //update theTime according to GPS time
                  prev_millis = millis();
                  int k = indices[i]+1;
                  theTime = (buffer[k]-48)*10 + (buffer[k+1]-48); //buffer[k] and buffer[k+1] are the 'H' and 'h' of 'Hh' respectively
                  //theTime now equals the hour of the day in Coordinated Universal Time (UTC)
                  //ASSUMPTION: currently assuming that implant will remain in Central Time Zone
                  theTime -= 5; //converts theTime to Central Time Zone
                  if (theTime < 0){
                    theTime += 24;
                  }
                  theTime *= 60; //converts theTime to minutes
                  theTime += (buffer[k+2]-48)*10 + (buffer[k+3]-48); //buffer[k+2] and buffer[k+3] are the 'M' and 'm' of 'Mm' respectively
                  //theTime is now updated (assuming gps time is correct)
                }

                //store latitude data
                if (i == 2 && lat_length < max_length){
                  Serial.println("");
                  Serial.print("Latitude Data: ");
                  for (int j=(indices[i]+1); j<indices[i+1]; j++){
                    Serial.print(char(buffer[j]));
                  }
                  int k = indices[i]+1;
                  unsigned long value1 = 10*(buffer[k]-48) + (buffer[k+1]-48);
                  unsigned long value2 = 0;
                  unsigned long pwrOf10 = 1;
                  for (int j=(indices[i+1]-1); j>(indices[i]+2); j--){
                    if (buffer[j] != '.'){
                      value1 *= 10;
                      value2 += (buffer[j]-48)*pwrOf10;
                      pwrOf10 *= 10;
                    }
                  }
                  value1 += value2*(10/6);
                  value2 = value1/10;
                  pwrOf10 = 1;
                  while (value2 != 0){
                    value2 /= 10;
                    pwrOf10 *= 10;
                  }
                  unsigned long temp1 = 0;
                  unsigned long temp2 = 0;
                  for (int j=0; j<7; j++){
                    if (j == 2){
                    lat[lat_length] = '.';
                    }
                    else if (j == 6){
                    lat[lat_length] = buffer[indices[i+1]+1];
                    }
                    else{
                    temp1 = (value1/pwrOf10);
                    lat[lat_length] = temp1 - temp2 + 48;
                    temp2 = temp1*10;
                    pwrOf10 /= 10;
                    }
                    lat_length++;
                  }
                  
                  lat[lat_length] = end_of_instance;
                  lat_length++;
                }

                //store longitude data
                if (i == 4 && lon_length < max_length){
                  Serial.println("");
                  Serial.print("Longitude Data: ");
                  for (int j=(indices[i]+1); j<indices[i+1]; j++){
                    Serial.print(char(buffer[j]));
                  }
                  int k = indices[i]+2;
                  unsigned long value1 = 10*(buffer[k]-48) + (buffer[k+1]-48);
                  unsigned long value2 = 0;
                  unsigned long pwrOf10 = 1;
                  for (int j=(indices[i+1]-1); j>(indices[i]+3); j--){
                    if (buffer[j] != '.'){
                      value1 *= 10;
                      value2 += (buffer[j]-48)*pwrOf10;
                      pwrOf10 *= 10;
                    }
                  }
                  value1 += value2*(10/6);
                  value2 = value1/10;
                  pwrOf10 = 1;
                  while (value2 != 0){
                    value2 /= 10;
                    pwrOf10 *= 10;
                  }
                  unsigned long temp1 = 0;
                  unsigned long temp2 = 0;
                  for (int j=0; j<7; j++){
                    if (j == 2){
                    lon[lon_length] = '.';
                    }
                    else if (j == 6){
                    lon[lon_length] = buffer[indices[i+1]+1];
                    }
                    else{
                    temp1 = (value1/pwrOf10);
                    lon[lon_length] = temp1 - temp2 + 48;
                    temp2 = temp1*10;
                    pwrOf10 /= 10;
                    }
                    lon_length++;
                  }
                  
                  lon[lon_length] = end_of_instance;
                  lon_length++;
                }

                //store date data
                if (i == 8 && date_length < max_length){
                  Serial.println("");
                  Serial.print("Date Data: ");
                  for (int j=(indices[i]+1); j<indices[i+1]; j++){
                    Serial.print(char(buffer[j]));
                    date[date_length] = buffer[j];
                    date_length++;
                  }
                  date[date_length] = end_of_instance;
                  date_length++;
                }
              } //end "for (int i=0;i<12;i++)"

              Serial.println("");
              Serial.print("GPS Time: ");
              Serial.print(theTime);
              Serial.print(" min. = ");
              int tt = theTime/60;
              int mm = theTime - (tt*60);
              Serial.print(tt);
              Serial.print(":");
              if (mm <= 9){
                Serial.print(0);
              }
              Serial.print(mm);
              Serial.println(" CST");

              ping_successful = true; //confirm that gps data was successfully received
              if (StartUp == true){
                StartUp = false; //start-up time has been determined, implant is no longer in start-up mode
              }

              if (transmit_successful == true){
                transmit_successful = false;
              }

            } //end "if (buffer[indices[1]+1] == 'A')"
          } //end "if (code_match == true)"

          //reset buffer
          buffer_length = 0;
          for (int i=0; i<300; i++){
            buffer[i] = 0;             
          }

        } //end "if (byteGPS == 13)"
      } //end "if (byteGPS == -1) else"
    } //end "while (ping_successful != true)"
    
    //digitalWrite(powerGPS, ); //power down gps
    Serial.println("");
    Serial.println("Waiting for next GPS ping or Base Station data transfer...");
  } //end "if (StartUp == true || theTime == 1200 || theTime == 0 || theTime == 240)"
  
  
  
  //TIME MONITORING CODE
  curr_millis = millis();
  if (prev_millis > curr_millis){ //check for millis() overflow and adjust if necessary
    //millis() has range of 0 to 4294967295, so it overflows after reaching 4294967295
    //overall, duration in minutes needs to equal ((4294967295 + 1) - (prev_millis - curr_millis))/60000
    //because of overflow issues, the above computation needs to be carried out in the following order of steps:
    duration = 4294967295;
    duration -= (prev_millis - curr_millis);
    duration += 1;
    duration /= 60000;
  }
  else{
    duration = (curr_millis - prev_millis)/60000; //duration equals the # of min since last time calculation
  }
  if (duration > 0){
    theTime += duration;
    theTime = theTime % 1440; //cycle through the 1440 minutes in 1 day
    //theTime is now updated. If theTime == 0, this means the time is 12am
    prev_millis = curr_millis;  
    ping_successful = false;
  }
    
  
  //BASE STATION COMMUNICATION CODE
  byteIn = transSerial_R.read(); //read a byte from the transceiver
  if (byteIn == base_signal && transmit_successful == false){ //check if the base station has been found. if so, send response and all data
    Serial.println("Base Station found. Preparing for data transfer");
    
    digitalWrite(TR_Pin, HIGH); //set transceiver to transmit
    pinMode(12, OUTPUT); //set transSerial_T tx pin as digital output
    
    //send response 6 times so the base station will not accidentally miss it
    for(int i=0; i<6; i++){
      transSerial_T.write(implant_response);
    }
    
    Serial.println("Sending data...");
    //send all stored data (in the message format)
    transSerial_T.write(byteStart);
    for(int i=0; i<lat_length; i++){
      transSerial_T.write(lat[i]);
    }
    transSerial_T.write(byteEnd);
    for(int i=0; i<lon_length; i++){
      transSerial_T.write(lon[i]);
    }
    transSerial_T.write(byteEnd);
    for(int i=0; i<time_length; i++){
      transSerial_T.write(time[i]);
    }
    transSerial_T.write(byteEnd);
    for(int i=0; i<date_length; i++){
      transSerial_T.write(date[i]);
    }
    transSerial_T.write(byteEnd);
    transSerial_T.write(byteStop);
    Serial.println("Data transfer complete");
    //reset lat, lon, time, and date buffers
    lat_length = 0;
    lon_length = 0;
    time_length = 0;
    date_length = 0;
    for (int i=0; i<max_length; i++){
      lat[i] = 0;
      lon[i] = 0;
      time[i] = 0;
      date[i] = 0;
    }
    digitalWrite(TR_Pin, LOW); //set transceiver back to receive
    pinMode(12, INPUT); //set transSerial_R rx pin as digital input
    transmit_successful = true;
    
    Serial.println("");
    Serial.println("Waiting for next GPS ping or Base Station data transfer...");
  } //end "if (byteIn == base_signal)"

  
} //end "loop"
