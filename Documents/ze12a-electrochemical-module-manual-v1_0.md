**Zhengzhou Winsen Electronics Technology Co.,Ltd                  www.winsen-sensor.com** 

## **ZE12A Electrochemical Gas Sensor Module** 

User’s Manual V1.0 （ Model ： ZE12A ） 

Valid from: 2020-03-20 

Zhengzhou Winsen Electronics Technology Co., Ltd 

Tel: 86-371-67169097/67169670  Fax: 86-371-60932988                                     Email: sales@winsensor.com **Leading gas sensing solutions supplier in China!** 

RR **Zhengzhou Winsen Electronics Technology Co.,Ltd                  www.winsen** ~~eo~~ **-sensor.com** 

## **Statement** 

This manual copyright belongs to Zhengzhou Winsen Electronics Technology Co., LTD. Without the 

written permission, any part of this manual shall not be copied, translated, stored in database or retrieval system, also can’t spread through electronic, copying, record ways. 

Thanks for purchasing our product. In order to let customers use it better and reduce the faults 

caused by misuse, please read the manual carefully and operate it correctly in accordance with the 

instructions. If users disobey the terms or remove, disassemble, change the components inside of the sensor, we shall not be responsible for the loss. 

The specific such as color, appearance, sizes …etc., please in kind prevail. 

We are devoting ourselves to products development and technical innovation, so we reserve the 

right to improve the products without notice. Please confirm it is the valid version before using this 

manual. At the same time, users’ comments on optimized using way are welcome. 

Please keep the manual properly, in order to get help if you have questions during the usage in the 

future. 

Zhengzhou Winsen Electronics Technology CO., LTD 

Tel: 86-371-67169097/67169670  Fax: 86-371-60932988                                     Email: sales@winsensor.com **Leading gas sensing solutions supplier in China!** 

**Zhengzhou Winsen Electronics Technology Co.,Ltd                  www.winsen-sensor.com** 

## **Electrochemical Detection Module ZE12A** 

ZE12A is a general-purpose and high-performance electrochemical module. It can detect the CO, SO2, NO2, O3 based on electrochemical principle, it has good selectivity and stability. A temperature sensor is built-in for temperature compensation. It has the digital output and analog voltage output at the same time which facilities the usage and calibration and shorten the development period. It is a combination of mature electrochemical detection principle and sophisticated circuit design, to meet customers’ different detection needs. 

## **Features** 

**==> picture [140 x 132] intentionally omitted <==**

**----- Start of picture text -----**<br>
E<br>**----- End of picture text -----**<br>


High sensitivity & resolution Low power consumption & long working life UART and analog voltage output 

Good stability and excellent anti-interference ability 

## **Main Application** 

City atmospheric environmental monitoring , enterprise environment monitoring, Factory area unorganized emission monitoring, emergency monitoring, environment evaluation monitoring, Portable gas detector,  various gas detection equipment and smart home appliance. 

## **Technical Parameters** 

|Model No.|ZE12A|
|---|---|
|Target Gas|CO,SO2,NO2,O3,H2S|
|Detection Range|CO(0-10ppm), SO2(0-1ppm),NO2(0-<br>1ppm),O3(0-1ppm),H2S(0-1ppm)|
|Output Data|DAC(0.4～2V)<br>standard voltage signal|
||UART Output(3V level, compatible<br>with 5V)|
|Workingvoltage|DC 5.0±0.1V|
|Response time|≤120 Sec|
|Resolution|≤10ppb|
|Dimension|Φ39×44 mm|
|Weight|75g|
|Operating Environment|Temp.: -20～50℃|
||Humidity.: 15%RH-90％RH<br>(no condensation)|
|Storage  Temp.|0～20℃|
|Lifespan|2years(in air)|



Figure 1:Module chart 

Tel: 86-371-67169097/67169670  Fax: 86-371-60932988                                     Email: sales@winsensor.com 

**Leading gas sensing solutions supplier in China!** 

RR **Zhengzhou Winsen Electronics Technology Co.,Ltd                  www.winsen-sensor.com** 

## **Pin definition** 

Table 2. 

|Pin1|Vout  (0.4～2 V)|
|---|---|
|Pin2|GND|
|Pin3|Vin (Voltage input)|
|Pin4|UART(TXD) data output|
|Pin5|UART(RXD) data input|



Figure 2: Module bottom view 

Accessories. ZE12A Socket (it is used to connect the module to users’ mainboard.) 

## **Detection range and signal output** 

|Detection gas|CO|SO2|NO2|O3|H2S|
|---|---|---|---|---|---|
|Detection range|0-10pm|0-1ppm|0-1ppm|0-1ppm|0-1ppm|
|Gas code|0x04|0x2B|0x2C|0x2A|0x03|



## **Concentration Unit Conversion** 

|Detection gas|CO|SO2|NO2|O3|H2S|
|---|---|---|---|---|---|
|Conversion Factor N|1.25|2.857|2.054|2.143|1.518|



In room temperature 0 ℃ , under a standard atmospheric pressure, the measured value [ug/m3] = [ppb] * gas relative molecular mass/air relative molecular mass. 

E.g.: relative molecular mass of CO is 28, while for air it is 22.4, thus N = 28/22.4 = 1.25. 

_ug_ / _m_ 3 Conversion Factor N= e.g.: If current concentration of CO is 500ppb, its ug/m3 is: _ppb_ , 1.25*500=625ug/m3. 

## **Accessories** 

Fool-proofing socket (it is necessary to connect user’s pcb board and module, and this accessory has pcb library, see note 7) 

## **Communication Protocol** 

## **1. General Settings** 

|**1. General Settings**||
|---|---|
|Baud Rate|9600|
|Data Bytes|8 bytes|
|Stop Byte|1 byte|
|check byte|Null|



## **2. Communication Specification** 

The default communication type is active upload and it sends gas concentration every one second. For example, if detect CO, the command line format is like below (Table 4). 

Tel: 86-371-67169097/67169670  Fax: 86-371-60932988                                     Email: sales@winsensor.com 

**Leading gas sensing solutions supplier in China!** 

**Zhengzhou Winsen Electronics Technology Co.,Ltd                  www.winsen-sensor.com** 

|0|1|2|3|4|5|6|7|8|
|---|---|---|---|---|---|---|---|---|
|Start<br>byte|Gas<br>name|Unit<br>PPB|no<br>decimal<br>point|gas<br>concentration<br>(high byte)|gas<br>concentration<br>(low byte)|Full<br>measurement<br>(high byte)|Full<br>measurement<br>(low byte)|Check value|
|0xFF|0x04|0x04|0x00|0x00|0x00|0x30|0XD4|0XF4|



Gas concentration value=concentration high byte*256+concentration low byte 

**Please note that** in the above calculation formula, the High byte and Low byte means the decimalism value changed from hexadecimal. 

## **Switch from active upload mode to question and answer mode, command line format as below** 

**(table 5)** 0 1 2 3 4 5 6 7 8 Start Reserve Switch Question reserve reserve reserve reserve Check byte command and value answer 0xFF 0x01 0x78 0x41 0x00 0x00 0x00 0x00 0X46 ~~SaReEeeee~~ **Switch to active upload mode, commands as following (table 6).** 0 1 2 3 4 5 6 7 8 Start Reserve Switch Actively reserve reserve reserve reserve Check byte comman upload value d 0xFF 0x01 0x78 0x40 0x00 0x00 0x00 0x00 0X47 ~~SRRREEEe~~ **To read gas concentration (table 7).** 0 1 2 3 4 5 6 7 8 Start Reserve command Reserve reserve reserve reserve reserve Check byte value 0xFF 0x01 0x86 0x00 0x00 0x00 0x00 0x00 0X79 ~~SSeS~~ **Sensor responses (table 8).** 0 1 2 3 4 5 6 7 8 Start Command gas concentration gas concentration reserve reserve Gas Gas Check byte concentration concentration value (high byte ug/m3) (low byte ug/m3) high byte (ppb) low byte (ppb) 0xFF 0x86 0x00 0x2A 0x00 0x00 0x00 0x20 0X30 ~~re~~ 

Gas concentration value=concentration high byte*256+concentration low byte 

## **3. Checksum and calculation** 

/********************************************************************** 

- Function Name: unsigned char FucCheckSum(uchar *i,ucharln) 

- Functional description: Sum check 【 Take 1\2\3\4\5\6\7 of sending and receving protocol Non+1 】 

- Function declaration: array[n]      NOT ﹛Sum （array[1]~array[n-1]） `}` +1 

Tel: 86-371-67169097/67169670  Fax: 86-371-60932988                                     Email: sales@winsensor.com 

**Leading gas sensing solutions supplier in China!** 

RR **Zhengzhou Winsen Electronics Technology Co.,Ltd                  www.winsen** ~~eo~~ **-sensor.com** 

（number of array must be larger than2） **********************************************************************/ 

unsigned char FucCheckSum(unsigned char *i,unsigned char ln) { unsigned char j,tempq=0; i+=1; for(j=0;j<(ln-2);j++) { tempq+=*i; i++; } tempq=(~tempq)+1; return(tempq); } 

## **Recommendations:** 

1. To ensure sensor’s accuracy, please calibrate the product regularly, it is generally recommended to calibrate it at least every six months. 

2. This electrochemical ZE12A sensor and the state control station have different working principles, their data will not be completely same, but their overall trend keep consistent. 

3. For customers who have long-term observation data, they can process the reading data every second, such as calculate the average value, and the processing interval can be set according to the reported time, such as 1min, 10mins or 1h. 

4. It is recommended that the data can be uploaded to the remote end such as the cloud or server for better query and calibration. 

5. Pls ensure that power supply is stable, since large ripple may cause fluctuation of values.  This ripple value is supposed to be lower than 30mV. 

6. The sensor is based on electrochemical principle and will be affected by external environment, such as temperature and humidity, air flow, electromagnetic fields, etc. Pls protect the sensor if it is used in extreme environment. 

7. For places where the temperature is too high, too low, or the temperature changes frequently, the sensor can be placed in a relatively normal or stable temperature environment, such as 20-25 ° C, by using heating and exhausting devices, thus to ensure ZE12A sensor can have better performance. 

8. If sensors stored in high or low humidity environment for a long time, this may cause internal electrolyte moisture changes, which will reduce its working life. It may cause damage for rough environment. Therefore, for high humidity environment, pls add waterproof and breathable devices/materials thus to dry the tube to protect the sensor. 

9. If the sensor is in an environment where wind speed changes greatly, it is recommended to add a micro air pump to ensure stability of air flow. Recommended low rate is 0.1-0.5L / min, maximum cannot exceed 1.0L/min. 

10. Sensor’s resolution is about 10ppb. If ambient concentration has minor changes, there may be a small or even constant change in displayed values. 

11. The module needs to provide stable power supply. Frequent power-off will cause serious deviations in displayed values. It is not recommended to use in intermittent power supply locations. Pls add a backup battery if frequent power-off is necessary. 

12. Electrochemical sensors start working after shipment, and it has no relationship whether it is powered on. Pls try to use the sensors as early as possible after receiving them. 

Tel: 86-371-67169097/67169670  Fax: 86-371-60932988                                     Email: sales@winsensor.com **Leading gas sensing solutions supplier in China!** 

RR **Zhengzhou Winsen Electronics Technology Co.,Ltd                  www.winsen-sensor.com** 

## **Cautions** 

- Please do not use the modules in systems which related to human being’s safety. 

- Please do not use the modules in strong air convection environment. 

- Please do not expose the modules in high concentration organic gas for a long time. 

- Sensor shall avoid organic solvent, coatings, medicine, oil and high concentration gases. 

- Excessive impact or vibration should be avoided, otherwise the value won’t be accurate. 

- The module should be charged for over 24hours for the first time, and supply circuit should be equipped with power reservation function. Otherwise, it will affect continuity and accuracy of returned data if it goes offline for too long. 

- The module should avoid direct sunlight, and fool-proof socket should be used to fix the module (PCB package library info pls contact salesperson). Its peripheral structure needs to be anti-rain, anti-shake and anti-drop from the socket. 

- When communicate with module, it is recommended to correspond a serial port with a module, thus make it convenient for later calibration and maintenance. 

- According to communication protocols, it is necessary to check whether byte0, byte1 and checksum are correct after receiving the data, thus to ensure correctness of receiving data frames. 

- It is suggested to use USB - convert - TTL tools and UART debug assistant software, and observe based on communication protocols to judge whether module communication is normal. 

**Zhengzhou Winsen Electronics Technology Co., Ltd Add:** No.299, Jinsuo Rd., Hi-Tech Zone, Zhengzhou 450001 China 

**Tel:** +86-371-67169097/67169670 **Fax:** +86-371-60932988 **E-mail:** sales@winsensor.com **Website:** www.winsen-sensor.com 

Tel: 86-371-67169097/67169670  Fax: 86-371-60932988                                     Email: sales@winsensor.com **Leading gas sensing solutions supplier in China!** 

