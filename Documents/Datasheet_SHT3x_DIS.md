## **Datasheet SHT3x-DIS** 

## Humidity and Temperature Sensor 

- Fully calibrated, linearized, and temperature compensated digital output 

- Wide supply voltage range, from 2.15 V to 5.5 V 

- I2C Interface with communication speeds up to 1 MHz and two user selectable addresses 

- Typical accuracy of  1.5 %RH and  0.1 °C for SHT35 

- NIST traceability 

- Very fast start-up and measurement time 

- Tiny 8-Pin DFN package 

## **Product Summary** 

SHT3x-DIS is the next generation of Sensirion’s temperature and humidity sensors. It builds on a new CMOSens® sensor chip that is at the heart of Sensirion’s new humidity and temperature platform. The SHT3x-DIS has increased intelligence, reliability and improved accuracy specifications compared to its predecessor. Its functionality includes enhanced signal processing, two distinctive and user selectable I2C addresses and communication speeds of up to 1 MHz. The DFN package has a footprint of 2.5 x 2.5 mm[2] while keeping a height of 0.9 mm. This allows for integration of the SHT3x-DIS into a great variety of applications. Additionally, the wide supply voltage range of 2.15 V to 5.5 V guarantees compatibility with diverse assembly situations. All in all, the SHT3x-DIS incorporates 15 years of knowledge of Sensirion, the leader in the humidity sensor industry. 

## **Benefits of Sensirion’s CMOSens[®] Technology** 

- High reliability and long-term stability 

- Industry-proven technology with a track record of more than 15 years 

- Designed for mass production 

- High process capability 

- High signal-to-noise ratio 

## **Content** 

- 1 Sensor Performance............................................. 2 2 Specifications ....................................................... 6 3 Pin Assignment .................................................... 8 4 Operation and Communication ............................. 9 5 Packaging ........................................................... 16 6 Shipping Package .............................................. 18 7 Quality ................................................................ 19 8 Ordering Information........................................... 19 9 Further Information ............................................. 19 

**==> picture [190 x 213] intentionally omitted <==**

**----- Start of picture text -----**<br>
nRESET<br>RH Sensor T Sensor<br>VDD<br>VSS<br>Power on<br>ADCADC<br>Reset<br>Calibration<br>Memory<br>Data  processing RESET<br>& Linearization<br>Digital Interface Alert  Logic<br>ADDR SDA SCL Alert<br>**----- End of picture text -----**<br>


**Figure 1** Functional block diagram of the SHT3x-DIS. The sensor signals for humidity and temperature are factory calibrated, linearized and compensated for temperature and supply voltage dependencies. 

1/22 

December 2022 - Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **1 Sensor Performance** 

## **Humidity Sensor Specification** 

|Parameter|Condition|Value|Units|
|---|---|---|---|
|SHT30 Accuracy tolerance1|Typ.|2|%RH|
||Max.|**Figure 2**|-|
|SHT31 Accuracy tolerance1|Typ.|2|%RH|
||Max.|**Figure 3**|-|
|SHT35 Accuracy tolerance1|Typ.|±1.5|%RH|
||Max.|**Figure 4**|-|
|Repeatability2|Low, typ.|0.21|%RH|
||Medium, typ.|0.15|%RH|
||High, typ.|0.08|%RH|
|Resolution|Typ.|0.01|%RH|
|Hysteresis|at 25°C|0.8|%RH|
|Specified range3|extended4|0 to 100|%RH|
|Response time5|63%|86|s|
|Long-term drift|Typ.7|<0.25|%RH/yr|



**Table 1** Humidity sensor specification. 

## **Temperature Sensor Specification** 

|Parameter|Condition|Value|Units|
|---|---|---|---|
|SHT30 Accuracy tolerance1|typ., 0°C to 65°C|0.2|°C|
||Max.|**Figure 8**|-|
|SHT31 Accuracy tolerance1|typ., 0°C to 90°C|0.2|°C|
||Max.|**Figure 9**|-|
|SHT35 Accuracy tolerance1|typ.,20°C to 60°C|±0.1|°C|
||Max.|**Figure 10**|-|
|Repeatability2|Low,typ.|0.15|°C|
||Medium,typ.|0.08|°C|
||High,typ.|0.04|°C|
|Resolution|Typ.|0.01|°C|
|Specified Range|-|-40 to 125|°C|
|Response time8|63%|>2|s|
|LongTerm Drift|max|<0.03|°C/yr|



**Table 2** Temperature sensor specification. 

> 1 For definition of typical and maximum accuracy tolerance, please refer to the document “Sensirion Humidity Sensor Specification Statement”. 

> 2 The stated repeatability is 3 times the standard deviation (3σ) of multiple consecutive measurements at the stated repeatability and at constant ambient conditions. It is a measure for the noise on the physical sensor output. Different measurement modes allow for high/medium/low repeatability. 

> 3 Specified range refers to the range for which the humidity or temperature sensor specification is guaranteed. 

> 4 For details about recommended humidity and temperature operating range, please refer to section 1.1. 

> 5 Time for achieving 63% of a humidity step function, valid at 25°C and 1m/s airflow. Humidity response time in the application depends on the design-in of the sensor. 

> 6 With activated ART function (see section 4.7) the response time can be improved by a factor of 2. 

> 7 Typical value for operation in normal RH/T operating range, see section 1.1. Maximum value is < 0.5 %RH/yr. Higher drift values might occur due to contaminant environments with vaporized solvents, out-gassing tapes, adhesives, packaging materials, etc. For more details please refer to Handling Instructions. 

> 8 Temperature response times strongly depend on the type of heat exchange, the available sensor surface and the design environment of the sensor in the final application. 

2/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **Humidity Sensor Performance Graphs** 

**==> picture [481 x 171] intentionally omitted <==**

**----- Start of picture text -----**<br>
D RH (%RH) D RH (%RH)<br>±8 ±8<br>maximal tolerance maximal tolerance<br>typical tolerance<br>±6 ±6 typical tolerance<br>±4 ±4<br>±2 ±2<br>±0 ±0<br>0 10 20 30 40 50 60 70 80 90 100 0 10 20 30 40 50 60 70 80 90 100<br>SHT30 Relative Humidity (%RH) SHT31 Relative Humidity (%RH)<br>**----- End of picture text -----**<br>


**Figure 2** Tolerance of RH at 25°C for SHT30. 

**Figure 3** Tolerance of RH at 25°C for SHT31. 

**==> picture [230 x 152] intentionally omitted <==**

**----- Start of picture text -----**<br>
D RH (%RH)<br>±6<br>maximal tolerance<br>typical tolerance<br>±4<br>±2<br>±0<br>0 10 20 30 40 50 60 70 80 90 100<br>SHT35 Relative Humidity (%RH)<br>**----- End of picture text -----**<br>


**Figure 4** Tolerance of RH at 25°C for SHT35. 

3/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

**==> picture [472 x 166] intentionally omitted <==**

**----- Start of picture text -----**<br>
SHT30 SHT31<br>RH (%RH)  RH (%RH)<br>100  ±4  ±4  ±4  ±4  ±4  ±4  ±4  ±4  100  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>90 ±3 ±3 ±3 ±3 ±3 ±3 ±3 ±3 90 ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>80  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  80  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>70  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  70  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>60  ±2  ±2  ±2  ±3  ±2  ±2  ±2  ±2  60  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>50  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  50  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>40  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  40  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>30  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  30  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>20  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  20  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>10  ±3  ±3  ±3  ±3  ±3  ±3  ±3  ±3  10  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>0  ±4  ±4  ±4  ±4  ±4  ±4  ±4  ±4  0  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>0  10  20  30  40  50  60  70  80  0  10  20  30  40  50  60  70  80<br>Temperature (°C)<br>Temperature (°C)<br>**----- End of picture text -----**<br>


**Figure 5** Typical tolerance of RH over T for SHT30. 

**Figure 6** Typical tolerance of RH over T for SHT31. 

**SHT35** 

**==> picture [229 x 153] intentionally omitted <==**

**----- Start of picture text -----**<br>
RH (%RH)<br>100  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>90  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2  ±2<br>80  ±2  ±2  ±2  ±2<br>70  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2  ±2<br>60  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2  ±2<br>50  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2  ±2<br>40  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2<br>30  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2<br>20  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2<br>10  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2<br>0  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±1.5  ±2<br>0  10  20  30  40  50  60  70  80<br>Temperature (°C)<br>**----- End of picture text -----**<br>


**Figure 7** Typical tolerance of RH over T for SHT35. 

4/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **Temperature Sensor Performance Graphs** 

**==> picture [481 x 203] intentionally omitted <==**

**----- Start of picture text -----**<br>
SHT30 SHT31<br>D T (°C) D T (°C)<br>±1.5 ±1.5<br>maximal tolerance maximal tolerance<br> typical tolerance typical tolerance<br>±1.0 ±1.0<br>±0.5 ±0.5<br>±0.0 ±0.0<br>-40 -20 0 20 40 60 80 100 120 -40 -20 0 20 40 60 80 100 120<br>Temperature (°C) Temperature (°C)<br>**----- End of picture text -----**<br>


**Figure 8** Temperature accuracy of the SHT30 sensor. 

**Figure 9** Temperature accuracy of the SHT31 sensor. 

**SHT35** 

**==> picture [233 x 190] intentionally omitted <==**

**----- Start of picture text -----**<br>
D T (°C)<br>±1.5<br>maximal tolerance<br>typical tolerance<br>±1.0<br>±0.5<br>±0.0<br>-40 -20 0 20 40 60 80 100 120<br>Temperature (°C)<br>**----- End of picture text -----**<br>


**Figure 10** Temperature accuracy of the SHT35 sensor. 

5/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

## **1.1 Recommended Operating Condition** 

The sensor shows best performance when operated within recommended normal temperature and humidity range of 5 °C – 60 °C and 20 %RH – 80 %RH, respectively. Long-term exposure to conditions outside normal range, especially at high humidity, may temporarily offset the RH signal (e.g. +3%RH after 60h kept at >80%RH). After returning into the normal temperature and humidity range the sensor will slowly come back to calibration state by itself. Prolonged exposure to extreme conditions may accelerate ageing. To ensure stable operation of the humidity sensor please refer to the document “Handling instructions SHT” Please note as well that this does apply not only to transportation and manufacturing, but also to operation of the SHT3x-DIS. 

## **2 Specifications** 

## **2.1 Electrical Specifications** 

|Parameter|Symbol|Condition|Min.|Typ.|Max.|Units|Comments|
|---|---|---|---|---|---|---|---|
|Supplyvoltage|VDD||2.15|3.3|5.5|V||
|Power-up/down level|VPOR||1.8|2.10|2.15|V||
|Slew rate change of the<br>supply voltage|VDD,slew||-|-|20|V/ms|Voltage changes on the<br>VDD line between<br>VDD,minand VDD,max<br>should be slower than<br>the maximum slew rate;<br>faster slew rates may<br>lead to reset;|
|Supply current|IDD|idle state<br>(single shot mode)<br>T=25°C|-|0.2|2.0|A|Current when sensor is<br>not performing a<br>measurement during<br>single shot mode|
|||idle state<br>(single shot mode)<br>T=125°C|-|-|6.0|||
|||idle state<br>(periodic data<br>acquisition mode)|-|45|-|A|Current when sensor is<br>not performing a<br>measurement during<br>periodic data acquisition<br>mode|
|||Measuring|-|600|1500|A|Current consumption<br>while sensor is<br>measuring|
|||Average|-|1.7|-|A|Current consumption<br>(operation with one<br>measurement per<br>second at lowest<br>repeatability, single shot<br>mode)|
|Alert Output driving<br>strength|IOH|||1.5x VDD||mA|See also section 3.5|
|Heater power|PHeater|Heater running|3.6|-|33|mW|Depending on the<br>supplyvoltage|



**Table 3** Electrical specifications, typical values are valid for T=25°C, min. & max. values for T=-40°C … 125°C 

6/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **2.2 Timing Specification for the Sensor System** 

|Parameter|Symbol|Conditions|Min.|Typ.|Max.|Units|Comments|
|---|---|---|---|---|---|---|---|
|Power-up time|tPU|After hard reset,<br>VDD≥ VPOR|-|0.5|1|ms|Time between VDDreaching<br>VPORand sensor entering idle<br>state|
|Soft reset time|tSR|After soft reset.|-|0.5|1.5|ms|Time between ACK of soft<br>reset command and sensor<br>enteringidle state|
|Duration of resetpulse|tRESETN||1|-|-|µs|See section 3.6|
|Measurement duration|tMEAS,l|Low repeatability|-|2.5|4|ms|The three repeatability modes<br>differ with respect to<br>measurement duration, noise<br>level and energyconsumption.|
||tMEAS,m|Medium repeatability|-|4.5|6|ms||
||tMEAS,h|High repeatability|-|12.5|15|ms||



**Table 4** System timing specification, valid from -40 °C to 125 °C and 2.4 V … 5.5 V. 

|Parameter|Symbol|Conditions|Min.|Typ.|Max.|Units|Comments|
|---|---|---|---|---|---|---|---|
|Power-up time|tPU|After hard reset,<br>VDD≥ VPOR|-|0.5|1.5|ms|Time between VDDreaching<br>VPORand sensor entering idle<br>state|
|Measurement duration|tMEAS,l|Low repeatability|-|2.5|4.5|ms|The three repeatability modes<br>differ with respect to<br>measurement duration, noise<br>level and energyconsumption.|
||tMEAS,m|Medium repeatability|-|4.5|6.5|ms||
||tMEAS,h|High repeatability|-|12.5|15.5|ms||



**Table 5** System timing specification, valid from -40 °C to 125 °C and 2.15 V … < 2.4V. 

## **2.3 Absolute Minimum and Maximum Ratings** 

Stress levels beyond those listed in Table 6 may cause permanent damage to the device or affect the reliability of the sensor. These are stress ratings only and functional operation of the device at these conditions is not guaranteed. Ratings are only tested each at a time. 

|Parameter|Rating|Units|
|---|---|---|
|Supplyvoltage VDD|-0.3 to 6|V|
|Max Voltage on pins (pin 1 (SDA); pin 2 (ADDR); pin 3 (ALERT); pin 4 (SCL); pin 6<br>(nRESET))|-0.3 to VDD+0.3|V|
|Input current on any pin|±100|mA|
|Operatingtemperature range|-40 to 125|°C|
|Storage temperature range|-40 to 150|°C|
|ESD HBM(human bodymodel)9|4|kV|
|ESD CDM(charge device model)10|750|V|



**Table 6** Minimum and maximum ratings; voltages may only be applied for short time periods. 

> 10 According to ANSI/ESD S5.3.1-2009; AEC-Q100-011. 

9 According to ANSI/ESDA/JEDEC JS-001-2014; AEC-Q100-002. 

7/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **3 Pin Assignment** 

The SHT3x-DIS comes in a 8-pin DFN package – see Table 7. 

|Pin|Name|Name|Comments|Comments|Comments|
|---|---|---|---|---|---|
|1|SDA||Serial data;input / output|||
|2|ADDR||Address pin; input; connect to either<br>logic high or low,do not leave floating|||
|3|ALERT||Indicates alarm condition; output; must<br>be left floatingif unused|||
|4|SCL||Serial clock;input / output|||
|5|VDD||Supplyvoltage;input|||
|6|nRESET||Reset pin active low; input; if not used it<br>is recommended to be left floating; can<br>be connected to VDD with a series<br>resistor of R ≥2 kΩ|||
|7|R||No electrical function; to be connected<br>to VSS|||
|8|VSS||Ground|||
|||1<br>2<br>3<br>4|||5<br>8<br>7<br>6|
|||||||
|||||||



**Table 7** SHT3x-DIS pin assignment (transparent top view). Dashed lines are only visible if viewed from below. The die pad is internally connected to VSS. 

## **3.1 Power Pins (VDD, VSS)** 

The electrical specifications of the SHT3x-DIS are shown in Table 3. The power supply pins must be decoupled with a 100 nF capacitor that shall be placed as close to the sensor as possible – see Figure 11 for a typical application circuit. 

## **3.2 Serial Clock and Serial Data (SCL, SDA)** 

SCL is used to synchronize the communication between microcontroller and the sensor. The clock frequency can be freely chosen between 0 to 1000 kHz. Commands with clock stretching according to I2C Standard[11 ] are supported. 

The SDA pin is used to transfer data to and from the sensor. Communication with frequencies up to 400 kHz must meet the I2C _Fast Mode_[11] standard. 

Communication frequencies up to 1 Mhz are supported following the specifications given in **Table 21** . 

Both SCL and SDA lines are open-drain I/Os with diodes to VDD and VSS. They should be connected to external pull-up resistors (please refer to Figure 11). A device on the I2C bus must only drive a line to ground. The external pull-up resistors (e.g. Rp=10 kΩ) are required to pull the signal high. For dimensioning resistor sizes please take bus capacity and communication frequency into account (see for example Section 7.1 of NXPs I2C Manual for more details[11] ). It should be noted that pull-up resistors may be included in I/O circuits of microcontrollers. It is recommended to wire the sensor according to the application circuit as shown in Figure 11. 

**==> picture [219 x 157] intentionally omitted <==**

**----- Start of picture text -----**<br>
VDD<br>RP RP<br>n RESET(6) VDD(5)<br>A LERT(3) SCL(4)<br>ADDR(2) SDA(1)<br>die<br>VSS(8) pad R(7)<br>100nF<br>**----- End of picture text -----**<br>


**Figure 11** Typical application circuit. Please note that the positioning of the pins does not reflect the position on the real sensor. This is shown in Table 7. 

## **3.3 Die Pad (center pad)** 

The die pad or center pad is visible from below and located in the center of the package. It is electrically connected to VSS. Hence electrical considerations do not impose constraints on the wiring of the die pad. However, due to mechanical reasons it is recommended to solder the center pad to the PCB. For more information on design-in, please refer to the document “SHTxx_STSxx Design Guide”. 

## **3.4 ADDR Pin** 

Through the appropriate wiring of the ADDR pin the I2C address can be selected (see Table 8 for the respective addresses). The ADDR pin can either be connected to logic high or logic low. The address of the sensor can be changed dynamically during operation by switching the level on the ADDR pin. The only constraint is that the level has to stay constant starting from the I2C start condition until the communication is finished. This allows to connect more than two SHT3x-DIS onto the same bus. 

> 11 http://www.nxp.com/documents/user_manual/UM10204.pdf 

8/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

The dynamical switching requires individual ADDR lines to the sensors. 

Please note that the I2C address is represented through the 7 MSBs of the I2C read or write header. The LSB switches between read or write header. The wiring for the default address is shown in Table 8 and Figure 11. The ADDR pin must not be left floating. Please note that only the 7 MSBs of the I2C Read/Write header constitute the I2C Address. 

||I2C Address in Hex.||
|---|---|---|
|SHT3x-DIS||<br>Condition|
||representation||
||||
|I2C address A|0x44 (default)|ADDR (pin 2)<br>connected to logic<br>low|
|I2C address B|0x45|ADDR (pin 2)<br>connected to logic<br>high|



**Table 8** I2C device addresses. 

## **3.5 ALERT Pin** 

The alert pin may be used to connect to the interrupt pin of a microcontroller. The output of the pin depends on the value of the RH/T reading relative to programmable limits. Its function is explained in a separate application note. If not used, this pin must be left floating. The pin switches high, when alert conditions are met. The maximum driving loads are listed in Table 3. Be aware that self-heating might occur, depending on the amount of current that flows. Self-heating can be prevented if the Alert Pin is only used to switch a transistor. 

## **3.6 nRESET Pin** 

The nReset pin may be used to generate a reset of the sensor. A minimum pulse duration of 1 µs is required to reliably trigger a reset of the sensor. Its function is explained in more detail in section 4. If not used it is recommended to leave the pin floating or to connect it to VDD with a series resistor of R ≥2 kΩ. However, the nRESET pin is internally connected to VDD with a pull up resistor of R = 50 kΩ (typ.). 

## **4 Operation and Communication** 

The SHT3x-DIS supports I2C fast mode (and frequencies up to 1000 kHz). Clock stretching can be enabled and disabled through the appropriate user command. For detailed information on the I2C protocol, refer to NXP I2C-bus specification[12] . 

After sending a command to the sensor a minimal waiting time of 1ms is needed before another command can be received by the sensor. 

All SHT3x-DIS commands and data are mapped to a 16bit address space. Additionally, data and commands are protected with a CRC checksum. This increases communication reliability. The 16 bits commands to the sensor already include a 3 bit CRC checksum. Data sent from and received by the sensor is always succeeded by an 8 bit CRC. 

In write direction it is mandatory to transmit the checksum, since the SHT3x-DIS only accepts data if it is followed by the correct checksum. In read direction it is left to the master to read and process the checksum. 

## **4.1 Power-Up and Communication Start** 

The sensor starts powering-up after reaching the powerup threshold voltage VPOR specified in Table 3. After reaching this threshold voltage the sensor needs the time tPU to enter idle state. Once the idle state is entered it is ready to receive commands from the master (microcontroller). 

Each transmission sequence begins with a START condition (S) and ends with a STOP condition (P) as described in the I2C-bus specification. Whenever the sensor is powered up, but not performing a measurement or communicating, it automatically enters idle state for energy saving. This idle state cannot be controlled by the user. 

## **4.2 Starting a Measurement** 

A measurement communication sequence consists of a START condition, the I2C write header (7-bit I2C device address plus 0 as the write bit) and a 16-bit measurement command. The proper reception of each byte is indicated by the sensor. It pulls the SDA pin low (ACK bit) after the falling edge of the 8th SCL clock to indicate the reception. A complete measurement cycle is depicted in Table 9. 

With the acknowledgement of the measurement command, the SHT3x-DIS starts measuring humidity and temperature. 

## **4.3 Measurement Commands for Single Shot Data Acquisition Mode** 

In this mode one issued measurement command triggers the acquisition of _one data pair_ . Each data pair consists of one 16 bit temperature and one 16 bit humidity value (in this order). During transmission each data value is always followed by a CRC checksum, see section 4.4. 

> 12 http://www.nxp.com/documents/user_manual/UM10204.pdf 

9/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

In single shot mode different measurement commands can be selected. The 16 bit commands are shown in Table 9. They differ with respect to repeatability (low, medium and high) and clock stretching (enabled or disabled). 

The repeatability setting influences the measurement duration and thus the overall energy consumption of the sensor. This is explained in section 2. 

|Condition|Condition|Hex.code|Hex.code|
|---|---|---|---|
|Repeatability|Clock<br>stretching|MSB|LSB|
|High|||06|
|Medium|enabled|0x2C|0D|
|Low|||10|
|High|||00|
|Medium|disabled|0x24|0B|
|Low|||16|
|e.g. 0x2C06: high repeatability measurement with clock||||
|stretchingenabled||||



sensor does not receive an ACK from the master after any byte of data, it will not continue sending data. 

The sensor will send the temperature value first and then the relative humidity value. After having received the checksum for the humidity value a NACK and stop condition should be sent (see Table 9). 

The I2C master can abort the read transfer with a NACK condition after any data byte if it is not interested in subsequent data, e.g. the CRC byte or the second measurement result, in order to save time. 

In case the user needs humidity and temperature data but does not want to process CRC data, it is recommended to read the two temperature bytes of data with the CRC byte (without processing the CRC data); after having read the two humidity bytes, the read transfer can be aborted with a with a NACK. 

## **No Clock Stretching** 

When a command without clock stretching has been issued, the sensor responds to a read header with a not acknowledge (NACK), if no data is present. 

## **Clock Stretching** 

When a command with clock stretching has been issued, the sensor responds to a read header with an ACK and subsequently pulls down the SCL line. The SCL line is pulled down until the measurement is complete. As soon as the measurement is complete, the sensor releases the SCL line and sends the measurement results. 

## **4.5 Measurement Commands for Periodic Data Acquisition Mode** 

**Table 9** Measurement commands in single shot mode. The first “SCL free” block indicates a minimal waiting time of 1ms. (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

## **4.4 Readout of Measurement Results for Single Shot Mode** 

After the sensor has completed the measurement, the master can read the measurement results (pair of RH& T) by sending a START condition followed by an I2C read header. The sensor will acknowledge the reception of the read header and send two bytes of data (temperature) followed by one byte CRC checksum and another two bytes of data (relative humidity) followed by one byte CRC checksum. Each byte must be acknowledged by the microcontroller with an ACK condition for the sensor to continue sending data. If the 

In this mode one issued measurement command yields _a stream of data pairs_ . Each data pair consists of one 16 bit temperature and one 16 bit humidity value (in this order). 

In periodic mode different measurement commands can be selected. The corresponding 16 bit commands are shown in Table 10. They differ with respect to repeatability (low, medium and high) and data acquisition frequency (0.5, 1, 2, 4 & 10 measurements per second, mps). Clock stretching cannot be selected in this mode. 

The data acquisition frequency and the repeatability setting influences the measurement duration and the current consumption of the sensor. This is explained in section 2 of this datasheet. 

If a measurement command is issued, while the sensor is busy with a measurement (measurement durations see Table 4), it is recommended to issue a break command first (see section 4.8). Upon reception of the 

10/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

break command the sensor abort the ongoing measurement and enter the single shot mode. 

|Condition|Condition|Hex. code|Hex. code|
|---|---|---|---|
|Repeatability|mps|MSB|LSB|
|High|0.5|0x20|32|
|Medium|||24|
|Low|||2F|
|High<br>Medium|1|0x21|30<br>26|
|<br>Low|||<br>2D|
|High|2|0x22|36|
|Medium|||20|
|Low|||2B|
|High|4|0x23|34|
|Medium|||22|
|Low|||29|
|High|10|0x27|37|
|Medium|||21|
|Low|||2A|
|e.g. 0x2130: 1 high repeatability mps - measurement per<br>second||||
|S<br>ACK<br>W<br>I2C Address<br>1<br>2<br>3<br>4<br>5<br>6<br>7<br>8<br>9<br>ACK<br>Command MSB<br>1<br>2<br>3<br>4<br>5<br>6<br>7<br>8<br>9<br>ACK<br>Command LSB<br>10<br>11<br>12<br>13<br>14<br>15<br>16<br>17<br>18<br>16-bit  command<br>I2C write header||||



**Table 10** Measurement commands for periodic data acquisition mode (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). N.B.: At the highest mps setting self-heating of the sensor might occur. 

## **4.6 Readout of Measurement Results for Periodic Mode** 

Transmission of the measurement data can be initiated through the fetch data command shown in Table 11. If no measurement data is present the I2C read header is responded with a NACK (Bit 9 in Table 11) and the communication stops. After the read out command fetch data has been issued, the data memory is cleared, i.e. no measurement data is present. 

**==> picture [225 x 156] intentionally omitted <==**

**----- Start of picture text -----**<br>
Command  Hex code<br>Fetch Data  0x E0 00<br>**----- End of picture text -----**<br>


**Table 11** Fetch Data command (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

## **4.7 ART Command** 

The ART (accelerated response time) feature can be activated by issuing the command in Table 12. After issuing the ART command the sensor will start acquiring data with a frequency of 4Hz. 

The ART command is structurally similar to any other command in Table 10. Hence section 4.5 applies for starting a measurement, section 4.6 for reading out data and section 4.8 for stopping the periodic data acquisition. 

The ART feature can also be evaluated using the Evaluation Kit EK-H5 from Sensirion. 

|Command|<br>Hex Code|
|---|---|
|Periodic Measurem<br>ART|ent with<br>0x2B32|
|S<br>ACK<br>W<br>I2C Address<br>1<br>2<br>3<br>4<br>5<br>6<br>7<br>8<br>9<br>ACK<br>Command MSB<br>1<br>2<br>3<br>4<br>5<br>6<br>7<br>8<br>9<br>ACK<br>Command LSB<br>10<br>11<br>12<br>13<br>14<br>15<br>16<br>17<br>18<br>16-bit  command<br>I2C write header||
|**Table 12**Command for a periodic data acquisition with<br>the ART feature (Clear blocks are controlled by the<br>microcontroller, grey blocks by the sensor).||



## **4.8 Break command / Stop Periodic Data Acquisition Mode** 

The periodic data acquisition mode can be stopped using the break command shown in Table 13. It is recommended to stop the periodic data acquisition prior to sending another command (except Fetch Data command) using the break command. Upon reception of the break command the sensor will abort the ongoing measurement and enter the single shot mode. This takes 1ms. 

11/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

|Command|Hex Code|
|---|---|
|Break|0x3093|
|||



**Table 13** Break command (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

## **4.9 Reset** 

A system reset of the SHT3x-DIS can be generated externally by issuing a command (soft reset) or by sending a pulse to the dedicated reset pin (nReset pin). Additionally, a system reset is generated internally during power-up. During the reset procedure the sensor will not process commands. 

In order to achieve a full reset of the sensor without removing the power supply, it is recommended to use the nRESET pin of the SHT3x-DIS. 

functionally identical to using the nReset pin. It is important to understand that a reset generated in this way is not device specific. All devices on the same I2C bus that support the general call mode will perform a reset. Additionally, this command only works when the sensor is able to process I2C commands. The appropriate command consists of two bytes and is shown in **Table 15** . 

**==> picture [226 x 115] intentionally omitted <==**

**----- Start of picture text -----**<br>
Command  Code<br>Address byte  0x00<br>Second byte  0x06<br>Reset command using the<br>0x0006<br>general call address<br>1 2 3 4 5 6 7 8 9 1 2 3 4 5 6 7 8 9<br>S General Call Address Reset Command<br>General Call 1 [st]  byte General Call 2 [nd]  byte<br>ACK ACK<br>**----- End of picture text -----**<br>


**Table 15** Reset through the general call address (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

## **Interface Reset** 

If communication with the device is lost, the following signal sequence will reset the serial interface: While leaving SDA high, toggle SCL nine or more times. This must be followed by a Transmission Start sequence preceding the next command. This sequence resets the interface only. The status register preserves its content. 

## **Soft Reset / Re-Initialization** 

The SHT3x-DIS provides a soft reset mechanism that forces the system into a well-defined state without removing the power supply. When the system is in idle state the soft reset command can be sent to the SHT3xDIS. This triggers the sensor to reset its system controller and reloads calibration data from the memory. In order to start the soft reset procedure the command as shown in Table 14 should be sent. 

It is worth noting that the sensor reloads calibration data prior to every measurement by default. 

|Command|Hex Code|
|---|---|
|Soft Reset|0x30A2|
|||



## **Reset through the nReset Pin** 

Pulling the nReset pin low (see Table 7) generates a reset similar to a hard reset. The nReset pin is internally connected to VDD through a pull-up resistor and hence active low. The nReset pin has to be pulled low for a minimum of 1 µs to generate a reset of the sensor. 

## **Hard Reset** 

A hard reset is achieved by switching the supply voltage to the VDD Pin off and then on again. In order to prevent powering the sensor over the ESD diodes, the voltage to pins 1 (SDA), 4 (SCL) and 2 (ADDR) also needs to be removed. 

## **4.10 Heater** 

The SHT3x is equipped with an internal heater, which is meant for plausibility checking only. The temperature increase achieved by the heater depends on various parameters and lies in the range of a few degrees centigrade. It can be switched on and off by command, see table below. The status is listed in the status register. After a reset the heater is disabled (default condition). 

**Table 14** Soft reset command (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

## **Reset through General Call** 

Additionally, a reset of the sensor can also be generated using the “general call” mode according to I2C-bus specification[12] . This generates a reset which is 

12/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

|Command<br>MS|Command<br>MS|Hex Code|
|---|---|---|
|||B<br>LSB|
|Heater Enable<br>0x3<br>Heater Disabled||0<br>6D<br>66|
||||
||||
||||



**Table 16** Heater command (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

## **4.11 Status Register** 

The status register contains information on the operational status of the heater, the alert mode and on the execution status of the last command and the last write sequence. The command to read out the status register is shown in Table 17 whereas a description of the content can be found in Table 18. 

|Command|Hex code|
|---|---|
|Read Out of status register|0xF32D|
|||



**Table 17** Command to read out the status register (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

|Bit|Field description|Default<br>value|
|---|---|---|
|15<br>|Alert pending status<br>'0': no pending alerts<br>'1': at least one pending alert<br>|‘1’<br>|
|14|Reserved|‘0’|
|13|Heater status<br>‘0’ : Heater OFF<br>‘1’: Heater ON|‘0’|
|12|Reserved|‘0’|
|11|RH tracking alert<br>‘0’ : no alert<br>‘1’. alert|‘0|
|10|T tracking alert<br>‘0’ : no alert<br>‘1’. alert|‘0’|
|9:5|Reserved|‘xxxxx’|
|4|System reset detected<br>'0': no reset detected since last ‘clear<br>status register’ command<br>'1': reset detected (hard reset, soft reset<br>command or supply fail)|<br>‘1’|
|3:2|<br>Reserved|‘00’|
|1<br>0|Command status<br>'0': last command executed successfully<br>'1': last command not processed. It was<br>either invalid, failed the integrated<br>command checksum<br>Wit dt hk tt|‘0’<br>‘0’|
||re aa cecsum saus<br>'0': checksum of last write transfer was<br>correct<br>'1':checksumof lastwrite transfer failed||



**Table 18** Description of the status register. 

## **Clear Status Register** 

All flags (Bit 15, 11, 10, 4) in the status register can be cleared (set to zero) by sending the command shown in Table 19. 

|Command|Hex Code|
|---|---|
|Clear status register|0x 30 41|
|||



**Table 19** Command to clear the status register (Clear blocks are controlled by the microcontroller, grey blocks by the sensor). 

## **4.12 Checksum Calculation** 

The 8-bit CRC checksum transmitted after each data word is generated by a CRC algorithm. Its properties are displayed in Table 20. The CRC covers the contents of the two previously transmitted data bytes. To calculate 

13/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

the checksum only these two previously transmitted data bytes are used. 

|Property|Value|
|---|---|
|Name|CRC-8|
|Width|8 bit|
|Protected data|read and/or write data|
|Polynomial|0x31(x8+ x5+ x4+ 1)|
|Initialization|0xFF|
|Reflect input|False|
|Reflect output|False|
|Final XOR|0x00|
|Examples|CRC(0xBEEF)= 0x92|



**Table 20** I2C CRC properties. 

## **4.13 Conversion of Signal Output** 

and compensated for temperature and supply voltage effects. Converting those raw values into a physical scale can be achieved using the following formulas. 

Relative humidity conversion formula (result in %RH): 

**==> picture [82 x 24] intentionally omitted <==**

Temperature conversion formula (result in °C & °F): 

**==> picture [115 x 54] intentionally omitted <==**

SRH and ST denote the raw sensor output for humidity and temperature, respectively. The formulas work only correctly when SRH and ST are used in decimal representation. 

Measurement data is always transferred as 16-bit values (unsigned integer). These values are already linearized 

## **4.14 Communication Timing** 

|Parameter|Symbol|Conditions|Min.|Typ.|Max.|Units|Comments|
|---|---|---|---|---|---|---|---|
|SCL clock frequency|fSCL||0|-|1000|kHz||
|Hold time (repeated) START<br>condition|<br>tHD;STA|After this period, the first<br>clockpulse isgenerated|0.24|-|-|µs||
|LOW period of the SCL<br>clock|tLOW||0.53|-|-|µs||
|HIGH period of the SCL<br>clock|tHIGH||0.26|-|-|µs||
|SDA hold time|tHD;DAT||0|-|250|ns|Transmittingdata|
||||0|-|-|ns|Receivingdata|
|SDA set-uptime|tSU;DAT||100|-|-|ns||
|SCL/SDA rise time|tR||-|-|300|ns||
|SCL/SDA fall time|tF||-|-|300|ns||
|SDA valid time|tVD;DAT||-|-|0.9|µs||
|Set-up time for a repeated<br>START condition|tSU;STA||0.26|-|-|µs||
|Set-up time for STOP<br>condition|tSU;STO||0.26|-|-|µs||
|Capacitive load on bus line|CB||-|-|400|pF||
|Low level input voltage|VIL||0|-|0.3xVDD|V||
|High level input voltage|VIH||0.7xVDD|-|1xVDD|V||
|Low level output voltage|VOL|3 mA sink current|-|-|0.4|V||



**Table 21** Timing specifications for I2C communication, valid for T=-40°C … 125°C and VDD = VDDmin … VDDmax. The nomenclature above is according to the I2C (UM10204, Rev. 6, April 4, 2014). 

14/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

**==> picture [238 x 164] intentionally omitted <==**

**----- Start of picture text -----**<br>
1/fSCL<br>tHIGH tLOW tR tF<br>70%<br>SCL<br>30%<br>tSU;DAT tHD;DAT<br>DATA IN<br>70%<br>SDA<br>7 esGE 30%<br>\ Fe tR<br>tVD;DAT tF<br>DATA OUT<br>70%<br>SDA<br>30%<br>**----- End of picture text -----**<br>


**Figure 12** Timing diagram for digital input/output pads. SDA directions are seen from the sensor. Bold SDA lines are controlled by the sensor, plain SDA lines are controlled by the micro-controller. Note that SDA valid read time is triggered by falling edge of preceding toggle. 

15/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **5 Packaging** 

SHT3x-DIS sensors are provided in an open-cavity DFN package. DFN stands for dual flat no leads. The humidity sensor opening is centered on the top side of the package. 

The sensor chip is made of silicon and is mounted to a lead frame. The latter is made of Cu plated with Ni/Pd/Au. Chip and lead frame are overmolded by an epoxy-based mold compound leaving the central die pad and I/O pins exposed for mechanical and electrical connection. Please note that the side walls of the sensor are diced and therefore these diced lead frame surfaces are not covered with the respective plating. 

The package (except for the humidity sensor opening) follows JEDEC publication 95, design registration 4.20, small scale plastic quad and dual inline, square and rectangular, No-LEAD packages (with optional thermal enhancements) small scale (QFN/SON), Issue D.01, September 2009. 

SHT3x-DIS has a Moisture Sensitivity Level (MSL) of 1, according to IPC/JEDEC J-STD-020. At the same time, it is recommended to further process the sensors within 1 year after date of delivery. 

## **5.1 Traceability** 

All SHT3x-DIS sensors are laser marked for easy identification and traceability. The marking on the sensor top side consists of a pin-1 indicator and two lines of text. 

The top line consists of the pin-1 indicator which is located in the top left corner and the product name. The small letter x stands for the accuracy class. 

The bottom line consists of 6 letters. The first two digits XY (=DI) describe the output mode. The third letter (A) represents the manufacturing year (4 = 2014, 5 = 2015, etc). The last three digits (BCD) represent an alphanumeric tracking code. That code can be decoded by Sensirion only and allows for tracking on batch level through production, calibration and testing – and will be provided upon justified request. 

If viewed from below pin 1 is indicated by triangular shaped cut in the otherwise rectangular die pad. The dimensions of the triangular cut are shown in Figure 14 through the labels T1 & T2. 

**==> picture [145 x 145] intentionally omitted <==**

**----- Start of picture text -----**<br>
SHT3 x<br>XYABCD<br>**----- End of picture text -----**<br>


**Figure 13** Top view of the SHT3x-DIS illustrating the laser marking. 

16/22 

December 2022- Version 7 

www.sensirion.com 

## Datasheet SHT3x-DIS 

## **5.2 Package Outline** 

**Figure 14** Dimensional drawing of SHT3x-DIS sensor package 

|Parameter|Symbol|Min|Nom.|Max|Units|Comments|
|---|---|---|---|---|---|---|
|Package height<br>~~ee es~~<br>~~Rs re~~|A<br>~~es~~<br>~~re~~|0.8<br>~~es~~<br>~~es~~|0.9<br>~~es~~<br>~~Ge GQ~~|1<br>~~GQ~~|mm<br>~~GQ~~|~~GQ~~|
|Leadframe height<br>~~ee es~~<br>~~Rs re~~<br>~~Rs~~|A3<br>~~es~~<br>~~re~~<br>|-<br>~~es~~<br>~~es~~<br>~~**e**e~~<br>|0.2<br>~~es~~<br>~~Ge GQ~~<br>~~es~~<br>|-<br>~~GQ~~<br>~~es~~<br>|mm<br>~~GQ~~<br>|~~GQ~~<br>|
|Pad width<br>~~Rs re~~<br>~~ee~~<br>~~Rs re~~|b<br>~~re~~<br>~~ee~~<br>~~re~~|0.2<br>~~es ~~<br>~~ee~~<br>~~**e**e~~<br>~~s~~|0.25<br> ~~Ge GQ~~<br>~~ee~~<br>~~es~~<br>~~Ge GQ~~|0.3<br>~~GQ~~<br>~~ee~~<br>~~es~~<br>~~GQ~~|mm<br>~~GQ~~<br>~~ee~~<br>~~GQ~~|~~GQ~~<br>~~ee~~<br>~~GQ~~|
|Package width<br>~~Rs re~~<br>~~Rs~~|D<br>~~re~~<br>|2.4<br>~~**e**e~~<br>~~s~~<br>~~ee~~<br>|2.5<br>~~es~~<br>~~Ge GQ~~<br>~~es~~<br>|2.6<br>~~es~~<br>~~GQ~~<br>~~es~~<br>|mm<br>~~GQ~~<br>|~~GQ~~<br>|
|Centerpad length<br>~~Rs re~~<br>~~ee~~<br>~~Rs~~<br>~~ee~~|D2<br>~~re ~~<br>~~ee~~<br>~~re~~<br>|1<br>~~**e**e~~<br> ~~s ~~<br>~~ee~~<br>~~ee~~<br>~~es~~<br>|1.1<br>~~es~~<br> ~~Ge GQ~~<br>~~ee~~<br>~~es~~<br>~~Ge GQ~~<br>|1.2<br>~~es~~<br>~~GQ~~<br>~~ee~~<br>~~es~~<br>~~GQ~~<br>|mm<br>~~GQ~~<br>~~ee~~<br>~~GQ~~|~~GQ~~<br>~~ee~~<br>~~GQ~~|
|Package length<br>~~Rs~~<br>~~ee~~|E<br>~~re~~<br>|2.4<br>~~ee~~<br>~~es~~<br>|2.5<br>~~es~~<br>~~Ge GQ~~<br>|2.6<br>~~es~~<br>~~GQ~~<br>|mm<br>~~GQ~~|~~GQ~~|
|Centerpad width<br>~~Rs~~<br>~~ee~~<br>~~ee~~|E2<br>~~re~~<br>~~ee~~<br>|1.7<br>~~ee~~<br>~~es~~<br>~~ee~~<br>|1.8<br>~~es~~<br>~~Ge GQ~~<br>~~se~~<br>|1.9<br>~~es~~<br>~~GQ~~<br>~~se~~<br>|mm<br>~~GQ~~<br>|~~GQ~~<br>|
|Padpitch<br><br>~~ee~~<br>~~ee ~~|e<br>~~re~~<br>~~ee~~<br> ~~es~~|-<br>~~es~~<br>~~ee~~<br>~~es ~~<br>~~es~~|0.5<br>~~Ge GQ~~<br>~~se~~<br> ~~ee~~<br>~~fe GQ~~|~~GQ~~<br>~~se~~<br>~~ee~~<br>~~GQ~~|mm<br>~~GQ~~<br>~~GG~~<br>~~GQ~~|~~GQ~~<br>~~GG~~<br>~~GQ~~|
|Pad length<br><br>~~ee ~~<br>~~se~~|L<br>~~ee~~<br> <br>~~se~~|0.25<br>~~ee ~~<br><br>~~se~~<br>~~es~~|0.35<br> ~~se~~<br><br>~~se~~<br>~~fe GQ~~|0.45<br>~~se~~<br><br>~~se~~<br>~~GQ~~|mm<br><br>~~se~~<br>~~GQ~~|~~se~~<br>~~GQ~~|
|Max cavity<br>~~se~~|S<br>~~se~~|-<br>~~se~~<br>~~es~~|-<br>~~se~~<br>~~fe GQ~~|1.5<br>~~se~~<br>~~GQ~~|mm<br>~~se~~<br>~~GQ~~|Only as guidance. This value includes all tolerances,<br>including displacement tolerances. Typically the opening<br>will be smaller.<br>~~se~~<br>~~GQ~~|
|Centerpad marking<br>~~es~~|T1xT2<br>~~es~~|-<br>~~es~~|0.3x45°<br>~~es~~|-<br>~~es~~|mm<br>~~es~~|indicates theposition ofpin 1<br>~~es~~|



**Table 22** Package outline. 

## **5.3 Land Pattern** 

Figure 15 shows the land pattern. The land pattern is understood to be the open metal areas on the PCB, onto which the DFN pads are soldered. 

The solder mask is understood to be the insulating layer on top of the PCB covering the copper traces. It is recommended to design the solder pads as a NonSolder Mask Defined (NSMD) type. For NSMD pads, the solder mask opening should provide a 60 μm to 75 μm design clearance between any copper pad and solder mask. As the pad pitch is only 0.5 mm we recommend to have one solder mask opening for all 4 I/O pads on one side. 

For solder paste printing it is recommended to use a laser-cut, stainless steel stencil with electro-polished trapezoidal walls and with 0.1 or 0.125 mm stencil thickness. The length of the stencil apertures for the I/O pads should be the same as the PCB pads. However, the position of the stencil apertures should have an offset of 0.1 mm away from the center of the package. The die pad aperture should cover about 70 – 90 % of the die pad area –thus it should have a size of about 0.9 mm x 1.6 mm. 

For information on the soldering process and further recommendation on the assembly process please consult the Handlin Instruction SHT, which can be found on the Sensirion webpage. 

17/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

Recommended Land Pattern 

Recommended Stencil Aperture 

**==> picture [183 x 142] intentionally omitted <==**

**----- Start of picture text -----**<br>
 2.35<br> 0.2   1   0.55<br> 0.5<br> 0.25<br> 0.5   1.7<br> 0.5<br>**----- End of picture text -----**<br>


**==> picture [188 x 143] intentionally omitted <==**

**----- Start of picture text -----**<br>
 2.55<br> 0.3   0.9   0.55<br> 0.5<br> 0.25<br> 0.5   1.6<br> 0.5<br>**----- End of picture text -----**<br>


**Figure 15** Recommended metal land pattern (left) and stencil apertures (right) for SHT3x-DIS. The dashed lines represent the outer dimension of the DFN package. The PCB pads (left) and stencil apertures (right) are indicated through the shaded areas. 

## **6 Shipping Package** 

**==> picture [440 x 221] intentionally omitted <==**

**----- Start of picture text -----**<br>
Ø1.5 +.1 /-0.0<br>4.00<br>2.00 ±.05 SEE Note 2 Ø1.00 MIN<br>4.00 SEE Note 1 1.75 ±.1<br>0.30 ±.05<br>A<br>5.50 ±.05<br>R 0.2 MAX. SEE NOTE 2<br>B0<br>B<br>12.0 +0.3/-0.1<br>A<br>K0 R 0.25 TYP.<br>A0<br>A  = 2.750<br>B  = 2.750<br>SECTION A - A K  = 1.200<br>TOLERANCES - UNLESS<br>NOTED 1PL ±.2  2PL  ±.10<br>NOTES:<br>1. 10 SPROCKET HOLE PITCH CUMULATIVE TOLERANCE ±0.2<br>2. POCKET POSITION RELATIVE TO SPROCKET HOLE MEASURED  DETAIL B<br>**----- End of picture text -----**<br>


2. POCKET POSITION RELATIVE TO SPROCKET HOLE MEASURED AS TRUE POSITION OF POCKET, NOT POCKET HOLE 3. A0 AND B0 ARE CALCULATED ON A PLANE AT A DISTANCE "R" ABOVE THE BOTTOM OF THE POCKET 

**Figure 16** Technical drawing of the packaging tape with sensor orientation in tape. Header tape is to the right and trailer tape to the left on this drawing. Dimensions are given in millimeters. 

18/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **7 Quality** 

Qualification of the SHT3x-DIS is performed based on the JEDEC JESD47 qualification test method. 

## **7.1 Material Contents** 

The device is fully RoHS and WEEE compliant, e.g. free of Pb, Cd, and Hg. 

## **8 Ordering Information** 

The SHT3x-DIS can be ordered in tape and reel packaging with different sizes, see Table 23. The reels are sealed into antistatic ESD bags. The document “SHT3x shipping package” that shows the 

details about the shipping package is available upon request. 

|Name|Quantity|Order Number|
|---|---|---|
|SHT30-DIS-B2.5kS|2500|1-101400-01|
|SHT30-DIS-B10kS|10000|1-101173-01|
|SHT31-DIS-B2.5kS|2500|1-101386-01|
|SHT31-DIS-B10kS|10000|1-101147-01|
|SHT35-DIS-B2.5kS|2500|1-101388-01|
|SHT35-DIS-B10kS|10000|1-101479-01|



**Table 23** SHT3x-DIS ordering options. 

## **9 Further Information** 

For more in-depth information on the SHT3x-DIS and its application please consult the documents in **Table 24** . Parameter values specified in the datasheet overrule possibly conflicting statements given in references cited in this datasheet. 

|Document Name|Description|Source|
|---|---|---|
|SHT3x Shipping Package|Information on Tape, Reel and shipping bags<br>(technical drawing and dimensions)|Available upon request|
|SHTxx_STSxx Design Guide|Design guidelines for designing SHTxx<br>humidity sensors into applications|Available for download at the Sensirion<br>humidity sensors download center:<br>www.sensirion.com/humidity-download|
|SHTxx Handling Instructions|Guidelines for proper handling of SHTxx<br>humidity sensors|Available for download at the Sensirion<br>humidity sensors download center:<br>www.sensirion.com/humidity-download|
|Sensirion Humidity Sensor<br>Specification Statement|Definition of sensor specifications.|Available for download at the Sensirion<br>humidity sensors download center:<br>www.sensirion.com/humidity-download|



**Table 24** Documents containing further information relevant for theSHT3x-DIS. 

19/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **Revision History** 

|Date|Version|Page(s)|Changes|
|---|---|---|---|
|October 2015|1||-|
|June 2016|2|2-4<br>6<br>7<br>7<br>11<br>17|Specifications for SHT35 added<br>ESD specifications updated<br>**Table 7**“Comments” section updated<br>**Figure 11**updated according to**Table 7**<br>Updated information about data memory to: “After the read out command<br>“fetch data” has been issued, the data memory is reset, i.e. no measurement<br>data is present.<br>Ordering information in**Table 23**updated|
|August 2016|3|6<br>7<br>7<br>8<br>8<br>4|Updated**Table 3**<br>Updated**Table 4**<br>Updated information on ESD testing norm<br>Updated**Table 7**<br>**Figure 11**and**Table 7**updated<br>**Figure 7**updated|
|March 2017|4|2-5<br>9<br>6<br>8<br>15<br>17<br>18<br>19|Updated RH&T accuracy specifications, see**Table 1**,**Table 2**,**Figure 2**,<br>**Figure 5**,**Figure 8**,**Figure 9**and**Figure 10**<br>**Table 8**updated<br>**Table 3**updated<br>**Figure 11**updated<br>**Table 21**updated<br>**Table 22**updated<br>**Figure 15**land pattern drawing simplified (no parameter changed)<br>Inlcuded: “Parameter values specified in the datasheet overrule possibly<br>conflicting statements given in references cited in this datasheet.“|



20/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

|22 May 2018|5|multiple<br>multiple<br>2<br>2<br>6<br>7<br>7<br>7<br>9<br>9<br>10<br>10<br>11<br>14|VDDmin=2.15V<br>Typo & formatting correction<br>Updated RH repeatability values in**Table 1**<br>Updated T repeatability and resolution in**Table 2**<br>**Table 3**<br>Updated VDDminand POR levels<br>Updated supply current values<br>Updated specification range<br>Updated soft reset time in**Table 4**<br>Introduced**Table 5**<br>Introduced “Ratings are only tested each at a time.” in section 2.3<br>Introduced “After sending a command to the sensor a minimal waiting time<br>of 1ms is needed before another command can be received by the sensor.”<br>In section 4<br>Removed: “The stop condition is optional.” in section 4.1<br>Updated label of**Table 9**with “The first “SCL free” block indicates a minimal<br>waiting time of 1ms.”<br>Updated section 4.5 to “Upon reception of the break command the sensor<br>abort the ongoing measurement and enter the single shot mode.”<br>Updated section 4.8 to “Upon reception of the break command the sensor<br>will abort the ongoing measurement and enter the single shot mode. This<br>takes 1ms.”<br>Updated**Table 21**|
|---|---|---|---|
|February 2019|6|19|Revised qualification test method in section 7|
|December|7|19|Updated**Table 24**due to obsolete document: Info covered in Handling<br>Instruction|



21/22 

December 2022- Version 7 

www.sensirion.com 

Datasheet SHT3x-DIS 

## **Important Notices** 

## **Warning, Personal Injury** 

**Do not use this product as safety or emergency stop devices or in any other application where failure of the product could result in personal injury. Do not use this product for applications other than its intended and authorized use. Before installing, handling, using or servicing this product, please consult the data sheet and application notes. Failure to comply with these instructions could result in death or serious injury.** 

If the Buyer shall purchase or use SENSIRION products for any unintended or unauthorized application, Buyer shall defend, indemnify and hold harmless SENSIRION and its officers, employees, subsidiaries, affiliates and distributors against all claims, costs, damages and expenses, and reasonable attorney fees arising out of, directly or indirectly, any claim of personal injury or death associated with such unintended or unauthorized use, even if SENSIRION shall be allegedly negligent with respect to the design or the manufacture of the product. 

## **ESD Precautions** 

The inherent design of this component causes it to be sensitive to electrostatic discharge (ESD). To prevent ESD-induced damage and/or degradation, take customary and statutory ESD precautions when handling this product. 

See application note “ESD, Latchup and EMC” for more information. 

## **Warranty** 

SENSIRION warrants solely to the original purchaser of this product for a period of 12 months (one year) from the date of delivery that this product shall be of the quality, material and workmanship defined in SENSIRION’s published specifications of the product. Within such period, if proven to be defective, SENSIRION shall repair and/or replace this product, in SENSIRION’S discretion, free of charge to the Buyer, provided that: 

- notice in writing describing the defects shall be given to SENSIRION within fourteen (14) days after their appearance; 

- such defects shall be found, to SENSIRION’s reasonable satisfaction, to have arisen from SENSIRION’s faulty design, material, or workmanship; 

- the defective product shall be returned to SENSIRION’s factory at the Buyer’s expense; and 

- the warranty period for any repaired or replaced product shall be limited to the unexpired portion of the original period. 

This warranty does not apply to any equipment which has not been installed and used within the specifications recommended by SENSIRION for the intended and proper use of the equipment. EXCEPT FOR THE WARRANTIES EXPRESSLY SET FORTH HEREIN, SENSIRION MAKES NO WARRANTIES, EITHER EXPRESS OR IMPLIED, WITH RESPECT TO THE PRODUCT. ANY AND ALL WARRANTIES, INCLUDING WITHOUT LIMITATION, WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE EXPRESSLY EXCLUDED AND DECLINED. SENSIRION is only liable for defects of this product arising under the conditions of operation provided for in the data sheet and proper use of the goods. SENSIRION explicitly disclaims all warranties, express or implied, for any period during which the goods are operated or stored not in accordance with the technical specifications. 

SENSIRION does not assume any liability arising out of any application or use of any product or circuit and specifically disclaims any and all liability, including without limitation consequential or incidental damages. All operating parameters, including without limitation recommended parameters, must be validated for each customer’s applications by customer’s technical experts. Recommended parameters can and do vary in different applications. 

SENSIRION reserves the right, without further notice, (i) to change the product specifications and/or the information in this document and (ii) to improve reliability, functions and design of this product. 

Copyright © 2019, by SENSIRION. CMOSens[®] is a trademark of Sensirion All rights reserved 

## **Headquarters and Subsidiaries** 

SENSIRION AG Laubisruetistr. 50 CH-8712 Staefa ZH Switzerland 

phone: +41 44 306 40 00 fax: +41 44 306 40 30 info@sensirion.com www.sensirion.com 

Sensirion Inc. USA Sensirion Korea Co. Ltd. phone: +1 312 690 5858 phone: +82 31 337 7700~3 info-us@sensirion.com info-kr@sensirion.com www.sensirion.com www.sensirion.co.kr Sensirion Japan Co. Ltd. Sensirion China Co. Ltd. phone: +81 3 3444 4940 phone: +86 755 8252 1501 info-jp@sensirion.com info-cn@sensirion.com www.sensirion.co.jp www.sensirion.com.cn/ 

Sensirion Taiwan Co. Ltd. phone:  +41 44 306 40 00 info@sensirion.com 

To find your local representative, please visit www.sensirion.com/contact 

22/22 

December 2022- Version 7 

www.sensirion.com 

