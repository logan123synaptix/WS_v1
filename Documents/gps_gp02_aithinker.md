

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page1of17
GP-02Specification
VersionV1.0
## Copyright©2021

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page2of17
## Disclaimerandcopyrightnotice
Theinformationinthisarticle,includingtheURLaddressforreference,issubjecttochangewithout
notice.Thedocumentisprovided“asis”andisnotliableforanysecurity,includinganysecurityfor
merchantability,applicationforaparticularpurposeornon-tort,andanyproposal,specificationor
samplereferredtoinhim.Thisdocumentshallnotberesponsibleforanykind,includingliabilityfor
theinfringementofanypatentrightsarisingfromtheuseoftheinformationinthisdocument.This
documentdoesnotherebygrantanyintellectualpropertyuselicense,whetherexpressorimplied,
inestoppelorotherwise.ThetestdataobtainedinthisarticlearefromAi-Thinkerlaboratorytest,
actualTheresultsmayvaryslightly.Itisherebystatedthatalltrademarknames,trademarksand
registeredtrademarksmentionedhereinarethepropertyoftheirrespectiveowners.
ThefinalinterpretationrightbelongstoShenzhenAi-ThinkerTechnologyCo.,Ltd.
## Note
## Duetoproductversionupgradesorotherreasons,thecontentsofthismanualmaybechanged.
ShenzhenAi-ThinkerTechnologyCo.,Ltd.reservestherighttomodifythecontentsofthismanual
withoutanynoticeorprompt.Thismanualisonlyusedasaguide.ShenzhenAi-Thinker
TechnologyCo.,Ltd.makeseveryefforttoprovideaccurateinformationinthismanual.However,
ShenzhenAi-ThinkerTechnologyCo.,Ltd.doesnotguaranteethatthecontentsofthemanualare
completelyfreeoferrors.AllstatementsandinformationinthismanualAndthesuggestiondoesnot
constituteanyexpressorimpliedguarantee.

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page3of17
## Documentdevelopment/revision/revocationresume
VersionDateRevisedcontentMakeVerify
## V1.02021.6.30
FirstEdition
## Qijing
## Zhang
## Ning
## Guan

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page4of17
## Content
1.Productoverview
..........................................................................................................................................5
1.1.Majorparameters
..............................................................................................................................6
2.Electricalparameters
...................................................................................................................................6
2.1.Performance
.......................................................................................................................................6
2.2.Analogcharacteristics
......................................................................................................................7
3.Dimensions
....................................................................................................................................................9
4.Pindefinition
................................................................................................................................................10
5.Schematicdiagram
....................................................................................................................................12
6.Designguidance
.........................................................................................................................................12
6.1.Applicationcircuit
............................................................................................................................12
6.2.Antennalayoutrequirements
........................................................................................................13
6.3.Powersupply
...................................................................................................................................13
6.4.UseofGPIOport
.............................................................................................................................14
7.Reflowsolderingcurve
..............................................................................................................................15
8.Packaging
....................................................................................................................................................16
9.Relatedmodules
.........................................................................................................................................16
10.Contactus
.................................................................................................................................................17

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page5of17
1.Productoverview
GP-02isahigh-performanceBDS/GNSSmulti-modesatellitenavigationreceiverSOC
module,whichintegratesRFfront-end,digitalbasebandprocessor,32-bitRISCCPU,
powermanagementandactiveantennadetectionandprotectionfunctions.Supporta
varietyofsatellitenavigationsystems,includingChina'sBeidousatellitenavigation
systemBDS,theUnitedStates'GPS,andRussia'sGLONASS,whichcanrealize
multi-systemjointpositioning.
Figure1Chiparchitecturediagram

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page6of17
1.1.Majorparameters
List1Mainparameterdescription
ModelGP-02
Size10.3*9.9*2.4(±0.2)MM
## Operating
temperature
## -40°C~85°C
## Storage
environment
## -40
## °C
## ~125
## °C
## ,<90%RH
## Powersupply
range
Voltage2.7V~3.6V
## Serialport
rate
## Maxsupport256000bps
CertificationRoHS
2.Electricalparameters
2.1.Performance
List2Electricalparameterdescription
## Technical
## Parameters
TestitemsValueUnit
## TTFF
## Coldstart≤32s
## Hotstart≤1s
## Recapture≤1s

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page7of17
2.2.Analogcharacteristics
List3Descriptionofanalogcharacteristics
No.ParametersCondition
## Parameters
## Unit
MinTypicalMax
## 1
## Resetvoltage
## @VDD_IO2.352.452.6V
## 2
## Resettime
## Crystalfrequency
26MHz
## 160ms
## 3
TCXOCrystalfrequency
26MHz
## Sensitivity
Coldstart-148dBm
Hotstart-156dBm
Recapture-160dBm
Trackingmode-162dBm
## Accuracy
## Positioningaccuracy<2
m
## （
## 1σ
## ）
## Timingaccuracy<30
ns
## （
## 1σ
## ）
## Speedmeasurement
accuracy
## <0.1
m/s
## （
## 1σ
## ）
Positioningupdaterate1Hz(Max5Hz)
## Power
consumption
BDS/GPSDualmode
continuousoperation
23mA
Sleepmode5mA
Standby8uA

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page8of17
## 4
TCXOamplitude
0.51.5Vpp
## 5
## Activeantennadetection
current
2.5mA
## 6
## Activeantennashort
circuitprotectioncurrent
455060mA
## 7
## Antennadetectioncircuit
voltagedrop
Input3.3V，50mAload
## 0.3V
## 8
## Workingcurrent
@3.3VBDS+GPS23mA
## 9
## Batterybackupcurrent
8uA
## 10
## Sleepmodecurrent
ON_OFF=05mA
## 11
RTCCrystalfrequency
32.768kHz
## 12
RTCCrystalEquivalent
seriesresistance
## 80KΩ
## 13
RTCCrystalSeries
capacitance
8pF

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page9of17
3.Dimensions
Figure2Appearancemap(picturesandsilkscreensareforreferenceonly,the
actualproductshallprevail)
Figure3Dimensions

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page10of17
4.Pindefinition
TheGP-02modulehasatotalof18interfaces.Asshowninthepindiagram,thepin
functiondefinitiontableistheinterfacedefinition.
Figure4Pindefinitiondiagram
List4Pinfunctiondefinition
No.NameFunctiondescription
1GNDGrounded
2TX0General-purposeGPIO,thedefaultisTXDofUART0
3RX0GeneralGPIO,thedefaultisRXDofUART0
4PPSTimepulsesignal
## 5N/F
## Shutdowncontrol,keephighlevelduringnormal
operation;internalpull-up
6VRTCBackuppowerforinternalRTC,1.4~3.6V
7NCNoconnect
83V33.3Vpowersupply
9RSTExternalresetinput,internalpull-up,itmustbeleft

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page11of17
floatingifnotused
10GNDGrounded
11RFRFinput
12GNDGrounded
## 13IO8
General-purposeGPIO,thedefaultisthemode
configuration.Whenhighlevelorfloating,itis
BDS+GPS;whenlowlevel,itisGPS+GLONASS.
14VRFPowersupplyanddetectionofactiveantenna
15NCNoconnect
16NCNoconnect
17NCNoconnect
18NCNoconnect

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page12of17
5.Schematicdiagram
Figure5Moduleschematic
6.Designguidance
6.1.Applicationcircuit

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page13of17
Figure6Applicationcircuitschematic
6.2.Antennalayoutrequirements
GP-02needstobeusedwithsolderingantenna,andthereareantennapadson
themodule.
Inordertoachievethebesteffectoftheantenna,thelocationoftheantenna
assemblyshouldbefarawayfrommetalpartsandhigh-frequencycomponents.
6.3.Powersupply
Recommend3.3Vvoltage,peakcurrentabove30mA.
ItisrecommendedtouseLDOforpowersupply;ifDC-DCisused,therippleis
recommendedtobecontrolledwithin50mV.
Itisrecommendedtoreservethepositionofthedynamicresponsecapacitorfor
theDC-DCpowersupplycircuittooptimizetheoutputripplewhentheload
changesgreatly.
ItisrecommendedtoaddESDdevicestothe3.3Vpowerinterface.

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page14of17
Figure7DC-DCreferencedesigndrawing
6.4.UseofGPIOport
TherearesomeGPIOportsontheperipheryofthemodule.Ifyouneedtouseit,it
isrecommendedtoconnecta10-100ohmresistorinserieswiththeIOport.This
cansuppressovershootandmakethelevelsonbothsidesmorestable.Itishelpful
forEMIandESD.
Forthepull-upandpull-downofspecialIOports,pleaserefertotheinstructionsin
thespecification,whichwillaffectthestartupconfigurationofthemodule.
TheIOportofthemoduleis3.3V.IfthemaincontrolandtheIOlevelofthemodule
donotmatch,alevelconversioncircuitneedstobeadded.
IftheIOportisdirectlyconnectedtoaperipheralinterfaceorterminalsuchasa
header,itisrecommendedtoreserveanESDdeviceneartheterminalontheIO
trace.

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page15of17
Figure8Levelconversioncircuit
7.Reflowsolderingcurve
## Figure9reflowsolderingcurve
!Attention
## Adjustthebalancetimetoensuretherationalizationofthegaswhenthesolderpaste
melts.IftherearetoomanygapsonthePCB,theequilibrationtimecanbeincreased.
## Consideringthattheproductisplacedintheweldingareaforalongtime(the
temperatureisabove180°C),inordertopreventdamagetothecomponentsandthe
bottomplate,theplacementtimeshouldbeshortenedasmuchaspossible.
## ！
## Importantcharacteristicsofthecurve:
## Ascent=1
## ～
4°C/sec,25°Cto150°C
Averagepreheattemperature=140°Cto150°C,60sec
## ～
## 90sec
Temperaturefluctuation=225°Cto250°C,about30sec
## Decentspeed=2
## ～
6°C/sec,to183°C,about15sec
## Totaltime=about300sec

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page16of17
8.Packaging
Asshowninthefigurebelow,thepackagingofGP-02isbraid.
## 。
Figure10Packagingtapingdiagram
9.Relatedmodules
List5Relatedmodeltable
ModelPowersupplyPackageSizeInterface
GP-01module3.3VSMD-2416.2*12.2*2.4(±0.2)MMUART
GP-02module3.3VSMD-1810.3*9.9*2.4(±0.2)MMUART
GP-01-Kit
development
board
## 5VSMD-826*24.1(±0.2)MMUART
GP-02-Kit
development
board
## 5VSMD-618*20.3(±0.2)MMUART
## Productrelatedinformation：https://docs.ai-thinker.com/gps

GP-02SpecificationV1.0
Copyright©2021ShenzhenAi-ThinkerTechnologyCo.,LtdAllRightsReserved
## Page17of17
10.Contactus
## Website：https://www.ai-thinker.com
DevelopmentDOCS：https://docs.ai-thinker.com
## Forum：http://bbs.ai-thinker.com
## Samplepurchase：https://aithinker.tmall.com
https://anxinke.taobao.com
## Business：overseas@aithinker.com
## Technicalsupport：support@aithinker.com
Address：Room410,BuildingC,HuafengIntelligenceInnovationPort,
Gushu,Xixiang,BaoanDistrict,ShenzhenChina518126
## Tel：0755-29162996