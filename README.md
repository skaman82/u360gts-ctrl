# u360gts-ctrl
u360gts Extension for the Mini-Case

<img src="https://raw.githubusercontent.com/skaman82/u360gts-ctrl/master/images/IMG.jpg"/>


This has the following functions:

• Turn ON/OFF the VTX</br>
• SmartAudio channel control (SA 2+ Version not supported, for unify nano you will need a 10k pull-down resistor on SA)</br>
• Battery monitoring</br>
• Controlls pan servo manually to allow to get to the screws of the case</br>
• Automatic parking mode to align the tracker again</br>


# PARTS
• Provided PCB

• Arduino Pro Micro (Leonardo) 3.3V
https://de.aliexpress.com/item/32992577032.html
If you have a 5V version, just replace the VREG on the board with this one: MIC5205-3.3YM5-TR

• 1206 4.7k resistor (R3)</br>
• 1206 27k resistor (R1)</br>
• 1206 100nf capacitor (C3)</br>
• 4x 0603 10k resistor (R2, R4, R5, R6)</br>
• 0603 1k resistor (R7)</br>
• IRLML6402 Mosfet (SOT23-3)</br>
• 2x BC848B Transistor (SOT23-3) (Q1, Q2)</br>

• 5-way tact switch
https://de.aliexpress.com/item/32860774374.html

• 128x32 I2C OLED Display
https://de.aliexpress.com/item/32965469301.html

• CD4066BM switch (SOP14 IC)
https://de.aliexpress.com/item/32461198916.html

• 3mm 940nm IR LEDs (receiver + transmitter) < 
receiver connects to GND and IR+ on the PCB; transmitter connects to 5v (through 100R resistor) and GND in the top box
https://de.aliexpress.com/item/32967272936.html


<strong>UPDATE 11. May 2020</strong><br>
Just spotted a bug on the PCB regarding the IR diode. I just corrected it in the new board file.
If you have the old version of the board, here is a workaround:<br><br>

• Remove R2 (don't throw it away)<br>
• bridge the two pads where R2 was sitting<br>
• solder the R2 to the outer side of the R6<br>
• connect 5V (BZ+) to the outer side of R2<br>
• Connect the IR LED (cathode on IR+, anode on IR-)<br>


<img src="https://raw.githubusercontent.com/skaman82/u360gts-ctrl/master/images/connections.png"/>
