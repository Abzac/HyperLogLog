The HyperLogLog algorithm [1] is a space efficient method to estimate the
cardinality of extraordinarily large data sets. This module provides an
implementation, written in C using a Murmur3 hash, for python 2.7.x or 
python 3.x.

[![Build Status](https://travis-ci.org/ascv/HyperLogLog.png?branch=master)](https://travis-ci.org/ascv/HyperLogLog)

v0.9

## Setup ##

You will need the python development package. On Ubuntu/Mint
you can install this package using:

    sudo apt-get install python-dev

Now install using pip:
    
    pip install HLL

Alternatively, install using setup.py:

    sudo python setup.py install

## Quick start ##

    from HLL import HyperLogLog
    
    hll = HyperLogLog(5) # use 2^5 registers
    hll.add('some data')
    estimate = hll.cardinality()
  
## Documentation ##

##### HyperLogLog(<i>k [,seed]) #####

Create a new HyperLogLog using 2^<i>k</i> registers, <i>k</i> must be in the 
range [2, 16]. Set <i>seed</i> to determine the seed value for the Murmur3 
hash. The default value is 314 (chosen arbitrarily).

* * *

##### add(<i>data</i>)

Adds <i>data</i> to the estimator where <i>data</i> is a string, buffer, or bytes
type.

##### merge(<i>HyperLogLog</i>)

Merges another HyperLogLog. Merging takes the maximum value for each
register and sets the current HyperLogLog's register to that value. The registers
of the other HyperLogLog are unaffected. 

##### murmur3_hash(<i>data [,seed]</i>)

Gets a signed integer from a Murmur3 hash of <i>data</i> where <i>data</i> is a 
string, buffer, or bytes (python 3.x). Set <i>seed</i> to determine the seed
value for the Murmur3 hash. The default seed is 314.

##### registers()

Gets a bytearray of the registers.

##### seed()

Gets the seed value used in the Murmur3 hash.

##### set_register(<i>index, value</i>)

Sets the register at <i>index</i> to <i>value</i>. Indexing is zero-based.

##### size()

Gets the number of registers.

## License

This software is released under the [MIT License](https://gist.github.com/ascv/5123769).

## References

[1] http://algo.inria.fr/flajolet/Publications/FlFuGaMe07.pdf
