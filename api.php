<?php

class MemcachedLite {

    const DISTRIBUTION_MODULO;
    const DISTRIBUTION_KETAMA;
    const DISTRIBUTION_VIRTUAL_BUCKET;
    
    public function __construct ($persistentId = null, $callable = null);

    public function add_server ($host, $port = 11211, $weight = 0);
    
    public function get_servers ();

    public function set ($key, $value, $ttl = 0, $cas = null);

    public function add ($key, $value, $ttl = 0);

    public function get ($key, &$exists = null, $cas = null);

    public function exists ($key);

    public function touch ($key, $ttl = 0);
    
    public function delete ($key);
    
    public function increment ($key, $offset = 1);

    public function decrement ($key, $offset = 1);

    // Setters and getters
    public function set_distribution ($type);
    public function get_distribution ();

    public function set_binary_protocol ($toggle);
    public function get_binary_protocol ();

    public function set_compression ($toggle);
    public function get_compression ();

}
