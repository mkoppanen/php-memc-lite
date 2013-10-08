What is this?
-------------

I needed a light-weight and simple API to memcached. PHP's memcached extension
was way too heavy for my needs. The memcache extension (without d) seems to be 
fairly unmaintained.

The API is limited to very basic set of operations. If you require a more detailed
API then PHP memcached extension is a better choice.

The API throws exceptions for exceptional cases, such as connection failures etc.
Key not found is not considered exceptional and will cause null to be returned

Building
--------

Install fairly recent libmemcached and run:

    phpize
    ./configure
    make
    make test
    make install

If tests fail, just give me a ping.

FastLZ
------

This extension uses bundled FastLZ. The license is available at fastlz/LICENSE

TODO
----

- Persistent connections