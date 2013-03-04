3/4/2013 - jab
The HAL sensor code in this repo are implemented utilizing the Dynamic Android Sensor HAL (aka DASH). 
Please refer to https://github.com/sonyxperiadev/DASH

The drivers currently within this repository are for the following AMS-TAOS US sensors.

Files:
tsl2772.c - AMS-TAOS US 2772x proximity sensor
tsl2772_als.c - AMS-TAOS US 2772x Ambient Light sensor
tmd3xxx_als.c - AMD-TAOS tmd/tsl3772 & tmd/tsl3782 ambient light senors.
tmd3xxx_als.c - AMD-TAOS tmd/tsl3772 & tmd/tsl3782proximity senors.
tmd3xxx_rgb.c - AMD-TAOS tmd/tsl3772 & tmd/tsl3782 ambient light senors. (Note: because there is no current "Android" rgb sensor type, 
this code passes the R/G/B raw data via accelerometer type where x=red, y=green, z=blue).  

This HAL code is non-conformant with typical Android HAL implementations and is provided solely for convenience.
These drivers have been developed and tested with Android (ver 4.1.1), running Linux kernel 3.0.31.


		page 1/1

