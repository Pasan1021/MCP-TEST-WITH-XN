// NORVI XN: counter logged once per second. With USB CDC On Boot disabled,
// Serial output is routed to the co-processor / Neural Pulse.
unsigned long counter = 0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.printf("counter=%lu\n", counter++);
  delay(1000);
}
