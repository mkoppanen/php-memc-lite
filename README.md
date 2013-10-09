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

FAQ
---

Disclaimer: no one actually asked these questions. I made them up.

#### Q: Can you add serializer X (igbinary, json etc) ?

No. It adds unnecessary complexity. If you want different serializer, just serialise
to string before saving. The extension stores strings as is.

#### Q: How do persistent connections work?

That's a very good question. The constructor takes two parameters: id and callable.
The callable is invoked only when a new internal structure is created. The callable
is an ideal place to add servers to make sure you only add them once per structure.

Throwing exception in the callable will propagate up and it will be thrown in the outer
scope.

Example:

    <?php
    $lite = new MemcachedLite ('my_conn', function (MemcachedLite $obj, $persistent_id) {
                                              echo "Initialising {$persistent_id}" . PHP_EOL;
                                              $obj->set_binary_protocol (true);
                                              $obj->add_server ('127.0.0.1', 11211);
                                          });
    // Use $lite here

#### Q: How do you handle returning uint64_t CAS tokens to PHP?

CAS token overflow is handled as a numeric string. Up until LONG_MAX the extension returns
integers, but larger CAS tokens get converted to numeric strings.

The user of the library should be careful not to do arithmetics with CAS tokens without
checking the type first or alternatively using a library that handles arbitrary length
integers.