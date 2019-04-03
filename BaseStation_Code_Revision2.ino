#include <SoftwareSerial.h>

// Any "Serial.print(...);" or "Serial.println(...);" is for debugging purposes only (outputting info
// to screen when the base station is connected to a computer). These statements can be removed
// or commented out if desired, without affecting the unit's intended behavior (hopefully).


//PROTOTYPES FOR USER-DEFINED FUNCTIONS
void set_default_pin_modes(void);
void set_transceiver_power(String);
void set_transceiver_mode(String);
void set_serial_baud_rates(int);
void reset_gps_data_arrays(void);
void prepare_datalogger(void);
void broadcast_detection_signal(int);
boolean listen_for_response(int);
void receive_implant_data(void);
void store_data(char *, int *);
void transfer_to_datalogger(void);

//PIN VARIABLES
int debug_rx_Pin = 0; //arduino serial input pin, used for debugging only (output to computer screen)
int debug_tx_Pin = 1; //arduion serial output pin, used for debugging only (output to computer screen)
int transceiver_serial_Pin = 4; //used as a SoftwareSerial rx pin for the arduino to connect to tx of Transceiver
int datalogger_serial_Pin = 3; //used as a SoftwareSerial tx pin for the arduion to connect to rx of Data Logger
int tr_select_Pin = 2; //transceiver transmit/receive select pin (HIGH = transmit, LOW = receive)
int pdn_Pin = 9; //transceiver power down pin (HIGH = power on, LOW = power down) this pin should always be HIGH
//synchronization pins for data logger:
int rts_Pin = 8; //rts stands for "Request To Send"
int cts_Pin = 7; //cts stands for "Clear To Send"

//TRANSCEIVER INTERFACE & IMPLANT COMMUNICATION VARIABLES
SoftwareSerial serial_transceiver_receive(4, 5); //4 is arduino rx, which connects to Transceiver tx, only receiving from Transceiver (5 not used)
SoftwareSerial serial_datalogger_transmit(2, 3); //3 is arduino tx, which connects to Data Logger rx, only writing to the Data Logger (2 not used, except for tr_select_Pin)
boolean response_detected = false;
char serial_byte = 0; //variable for temporarily storing incoming transceiver byte
char base_signal = 63; //detection byte that the base station is broadcasting (decimal 63 equals a '?' character)
char implant_response = 33; //response byte that the base station listens for (decimal 33 equals a '!' character)

//DATA STORAGE & MESSAGE STRUCTURE VARIABLES
int data_set = 1; //this number is used to keep track of how many data sets (encounters with the implant) have been obtained
const int max_data_arrays_length = 100;
char latitudes[max_data_arrays_length]; //buffer for storing latitude values
char longitudes[max_data_arrays_length]; //buffer for storing longitude values
char times[max_data_arrays_length]; //buffer for storing time-stamp values
char dates[max_data_arrays_length]; //buffer for storing date-stamp values
int latitudes_length = 0; //gives the length of latitudes being used
int longitudes_length = 0; //gives the length of longitudes being used
int times_length = 0; //gives the length of times being used
int dates_length = 0; //gives the length of dates being used
char start_byte = 1; //the start byte is a "start of heading" character
char end_byte = 13; //the end byte is a "new line" character
char stop_byte = 4; //the stop byte is a "end of transmission" character
char end_of_instance = 10; //used to separate instances of gps data (decimal 13 = 'carriage return')
//MESSAGE FORMAT: start_byte, latitudes, end_byte, longitudes, end_byte, times, end_byte, dates, end_byte, stop_byte, start_byte   







void setup() {
  
  set_default_pin_modes();
  set_transceiver_power("on");
  set_serial_baud_rates(9600);
  reset_gps_data_arrays();  
  prepare_datalogger();
  Serial.println("Setup Complete");
  
}



void loop() {
    
  //The scheme for broadcasting a detection signal 5 times and listening for a response 7 times (seen below) is
  // somewhat arbitrary, but this scheme makes sure the Base Station won't accidentally miss hearing a response
  // signal from the Implant while it (the base station) is still sending out a detection signal. 
 
  //Example of scheme during 2 extremes:
  //     (note: The Implant constantly listens, once it hears a "?", it sends a "!" 6 times. The 6 allows the scheme to work)
  //     (note: L stands for Listen)
  //
  //  Extreme Scenario 1 - The Implant hears the first "?" and immediately starts responding (worst scenario)
  //     Base Station: ?, ?, ?, ?, ?, L, L, L, L, L, L, L
  //          Implant: L, !, !, !, !, !, !, ... <-- For safety, this scheme gives the base station 2 chances at detecting the "!"
  //     
  //  Extreme Scenario 2 - The Implant hears the last "?" (best scenario)
  //     Base Station: ?, ?, ?, ?, ?, L, L, L, L, L, L, L
  //          Implant: .........., L, !, !, !, !, !, !, ... 
  
  broadcast_detection_signal(5); //send out the detection signal 5 times
  response_detected = listen_for_response(7); //attempt to receive implant response 7 times
  
  if (response_detected == true){
    receive_implant_data();
    transfer_to_datalogger();
    reset_gps_data_arrays();
    data_set++; // 1 data set received successfully, so increment data_set for the next time
  }

}







//USER-DEFINED FUNCTIONS
void set_default_pin_modes(){
  pinMode(debug_rx_Pin, INPUT);
  pinMode(debug_tx_Pin, OUTPUT);
  pinMode(rts_Pin,OUTPUT);
  pinMode(cts_Pin,INPUT);
  pinMode(tr_select_Pin, OUTPUT);
  pinMode(pdn_Pin, OUTPUT);
}

void set_transceiver_power(String power){
  if(power == "on"){
    digitalWrite(pdn_Pin, HIGH); //power up transceiver
  }
  else if(power == "off"){
    digitalWrite(pdn_Pin, LOW); //power down transceiver
  }
  else{}
}

void set_transceiver_mode(String mode){
  if(mode == "receive"){
    digitalWrite(tr_select_Pin, LOW);
    pinMode(transceiver_serial_Pin, INPUT);
  }
  else if(mode == "transmit"){
    digitalWrite(tr_select_Pin, HIGH);
    pinMode(transceiver_serial_Pin, OUTPUT);
  }
  else{}
}

void set_serial_baud_rates(int baud_rate){
  Serial.begin(baud_rate);
  serial_datalogger_transmit.begin(baud_rate);
  serial_transceiver_receive.begin(baud_rate); //transceiver data sheet says it can handle up to 10,000 baud rate
}

void reset_gps_data_arrays(){
  for(int i=0; i<max_data_arrays_length; i++) {
    latitudes[i] = 0;
    longitudes[i] = 0;
    times[i] = 0;
    dates[i] = 0;
  }
  latitudes_length = 0;
  longitudes_length = 0;
  times_length = 0;
  dates_length = 0;
}

void prepare_datalogger(){
  //Not sure why these commands need to happen, but they allow the data logger to work correctly after
  // they've been executed. Otherwise, writing to the data logger doesn't work
  digitalWrite(datalogger_serial_Pin, HIGH);
  digitalWrite(rts_Pin, LOW);    
  delay(2000); //the delay gives the data logger time to see that the "rts" is "LOW" (I think...)
  serial_datalogger_transmit.write("IPA");
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("E");
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("E");
  serial_datalogger_transmit.write(13);    
  serial_datalogger_transmit.write("e");
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("e");
  serial_datalogger_transmit.write(13);
}

void broadcast_detection_signal(int n){
  set_transceiver_mode("transmit"); //set transceiver to transmit mode
  for(int i=0; i<n; i++) {
    serial_transceiver_receive.write(base_signal); //send detection signal n times
  }
}

boolean listen_for_response(int n){
  set_transceiver_mode("receive"); //set transceiver to receive mode
  for(int i=0; i<n; i++){
      serial_byte = serial_transceiver_receive.read();
      if(serial_byte == implant_response) //if the response signal is detected
        return true; //immediately return "true" (the "return false;" statement below won't be reached
  }
  return false; //return "false" if the response signal isn't detected during the n attempts
}

void receive_implant_data(){
  //look for the start byte (some of the bytes being read may still be the response signal)
  while(serial_byte != start_byte || serial_byte != stop_byte) {
    serial_byte = serial_transceiver_receive.read();
    Serial.print(serial_byte);
  } //end “while"
  Serial.println("");
  
  store_data(&latitudes[latitudes_length], &latitudes_length);
  store_data(&longitudes[longitudes_length], &longitudes_length);
  store_data(&times[times_length], &times_length);
  store_data(&dates[dates_length], &dates_length);
}

void store_data(char * array_element_ptr, int * length_ptr){
  serial_byte = serial_transceiver_receive.read();
  //look for the end byte and store all previous bytes in the corresponding data buffer
  while(serial_byte != end_byte && *length_ptr < max_data_arrays_length) {
    if(serial_byte != -1){
      Serial.print(char(serial_byte));
      *array_element_ptr = serial_byte;
      array_element_ptr++;
      (*length_ptr)++;
    }
    serial_byte = serial_transceiver_receive.read();
  } //end “while”
  Serial.println("");
}

void transfer_to_datalogger(){
  Serial.println("Writing data to USB stick");

  //open file
  serial_datalogger_transmit.write("OPW ");
  serial_datalogger_transmit.write("GPSdata.txt");
  serial_datalogger_transmit.write(13);

  //write times data to file
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(6);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("Time: ");
  serial_datalogger_transmit.write(13);
  delay(1000);  
  for (int i = 0; i < times_length; i++){
    serial_datalogger_transmit.write("WRF ");
    serial_datalogger_transmit.write(1);
    serial_datalogger_transmit.write(13);
    serial_datalogger_transmit.write(char(times[i]));
    serial_datalogger_transmit.write(13);
    delay(1000);
  }

  //write dates data to file
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(6);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("Date: ");
  serial_datalogger_transmit.write(13);
  delay(1000); 
  for (int i = 2; i < 4; i++){
    serial_datalogger_transmit.write("WRF ");
    serial_datalogger_transmit.write(1);
    serial_datalogger_transmit.write(13);
    serial_datalogger_transmit.write(char(dates[i]));
    serial_datalogger_transmit.write(13);
    delay(1000);
  }
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(1);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("/");
  serial_datalogger_transmit.write(13);
  delay(1000); 
  for (int i = 0; i < 2; i++){
    serial_datalogger_transmit.write("WRF ");
    serial_datalogger_transmit.write(1);
    serial_datalogger_transmit.write(13);
    serial_datalogger_transmit.write(char(dates[i]));
    serial_datalogger_transmit.write(13);
    delay(1000);
  }
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(1);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("/");
  serial_datalogger_transmit.write(13);
  delay(1000); 
  for (int i = 4; i < (dates_length-1); i++){
    serial_datalogger_transmit.write("WRF ");
    serial_datalogger_transmit.write(1);
    serial_datalogger_transmit.write(13);
    serial_datalogger_transmit.write(char(dates[i]));
    serial_datalogger_transmit.write(13);
    delay(1000);
  }
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(1);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write(10);
  serial_datalogger_transmit.write(13);
  delay(1000); 

  //write latitudes and longitudes data in hyperlink
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(11);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("http://www.");
  serial_datalogger_transmit.write(13);
  delay(1000);
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(11);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write("maps.google");
  serial_datalogger_transmit.write(13);
  delay(1000);  
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(12);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write(".com/maps?q=");
  serial_datalogger_transmit.write(13);
  delay(1000);
  for(int i=0; i<(latitudes_length-1); i++){
    serial_datalogger_transmit.write("WRF ");
    serial_datalogger_transmit.write(1);
    serial_datalogger_transmit.write(13);
    serial_datalogger_transmit.write(char(latitudes[i]));
    serial_datalogger_transmit.write(13);
    delay(1000);
  }
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(1);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write(",");
  serial_datalogger_transmit.write(13);
  delay(1000);
  for(int i=0; i<(longitudes_length-1); i++){
    serial_datalogger_transmit.write("WRF ");
    serial_datalogger_transmit.write(1);
    serial_datalogger_transmit.write(13);
    serial_datalogger_transmit.write(char(longitudes[i]));
    serial_datalogger_transmit.write(13);
    delay(1000);
  }
  serial_datalogger_transmit.write("WRF ");
  serial_datalogger_transmit.write(1);
  serial_datalogger_transmit.write(13);
  serial_datalogger_transmit.write(10);
  serial_datalogger_transmit.write(13);
  delay(1000);

  //close file
  serial_datalogger_transmit.write("CLF ");
  serial_datalogger_transmit.write("GPSdata.txt");
  serial_datalogger_transmit.write(13);

  Serial.println("Done writing to USB stick");
}
