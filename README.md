AHABus Ground Segment Parser
============================

    Version 0.1-A3
    Author: Amy parent <amy@amyparent.com>
    Date:   2017-04-23

Ground segment parser used to decode a packet radio stream coming from an AHABus
High-Altitude Radio mission. Requires the use of the modified version of
Dl-Fldigi available at <https://github.com/ahabus/dl-fldigi>.

## Usage

The parser is built by calling `make ahabus-parser`Once dl-fldigi is running,
the parser is started by calling `ahabus-parser`. System messages and location
are printed to `stdout`. Data and logs for each payload are saved in the current 
directory.
