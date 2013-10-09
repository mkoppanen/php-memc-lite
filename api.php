<?php
/**
 * Used for all exceptions in this extension
 *
 * @author Mikko Koppanen <mikko.koppanen@gmail.com>
 */
class MemcachedLiteException extends Exception {
}

/**
 * The main class of the extension
 *
 * @author Mikko Koppanen <mikko.koppanen@gmail.com>
 */
class MemcachedLite {

    const DISTRIBUTION_MODULO = 1;
    const DISTRIBUTION_KETAMA = 2;
    const DISTRIBUTION_VIRTUAL_BUCKET = 3;
    
    /**
     * Construct a new object. If persistent_id is given the underlying
     * handle is created persistently and can be used from request to request.
     *
     * @param string $persistent_id  (optional) Name for this persistent handle. Default: null
     * @param callable $callable     (optional)  Callback to be executed every time a new object is created
     *                               The signature of the callback is: (MemcachedLite $obj, $persistent_id = null). Default: null
     *
     */
    public function __construct ($persistent_id = null, $callable = null);

    /**
     * Adds a new server connection
     *
     * @param string  $host   The hostname or ip of the server
     * @param integer $port   (optional) The port of the server. Default: 11211
     * @param integer $weight (optional) Server weight, i.e. how large amount of keys get stored to this server. Default: 0
     * 
     * @return bool
     */
    public function add_server ($host, $port = 11211, $weight = 0);
    
    /**
     * Gets an array of currently added servers. Each server entry contains 'host' and 'port' keys
     *
     * @return array
     */
    public function get_servers ();

    /**
     * Returns the server host and port where the key hashes to.
     *
     * @param string  $key  The key to test
     * 
     * @return array containing 'host' and 'port' keys
     */
    public function get_server ($key);

    /**
     * Sets a key to a specific value
     *
     * @param string  $key   The key to store the value under
     * @param mixed   $value The value to store, can be any type (except anything related to resources)
     * @param integer $ttl   (optional) Time to live value for the key, in seconds. Default: 0
     * @param mixed   $cas   (optional) CAS token, can be acquired by calling get prior to set. Default: null
     *
     * @return bool
     */
    public function set ($key, $value, $ttl = 0, $cas = null);

    /**
     * Adds a key if it does not exist. If they key already exists no action is taken
     *
     * @param string  $key   The key to store the value under
     * @param mixed   $value The value to store, can be any type (except anything related to resources)
     * @param integer $ttl   (optional) Time to live value for the key, in seconds. Default: 0
     *
     * @return bool
     */
    public function add ($key, $value, $ttl = 0);

    /**
     * Gets a value stored under a key
     *
     * @param string  $key      The key for the value
     * @param bool    &$exists  (optional) Returns boolean indicating if they exists. Useful for checking if NULL value exists. Default: null
     * @param mixed   &$cas     (optional) CAS token. Default: null
     *
     * @return mixed
     */
    public function get ($key, &$exists = null, &$cas = null);

    /**
     * Checks whether a key exists
     *
     * @param string  $key  The key to test
     *
     * @return boolean indicating if the key exists
     */
    public function exists ($key);

    /**
     * Change time to live for a key
     *
     * @param string  $key  The key
     * @param integer $ttl   (optional) Time to live value for the key, in seconds. Default: 0
     *
     * @return boolean Returns true if the operation was successful and false if the key doesn't exist
     */
    public function touch ($key, $ttl = 0);

    /**
     * Deletes a key
     *
     * @param string  $key  The key to delete
     *
     * @return boolean indicating if the key was deleted
     */
    public function delete ($key);

    /**
     * Increment a key
     *
     * @param string  $key     The key
     * @param integer $offset  (optional) The amount to increment. Default: 1
     *
     * @return integer Returns the new value of the key
     */
    public function increment ($key, $offset = 1);

    /**
     * Decrement a key
     *
     * @param string  $key    The key
     * @param integer $offset (optional) The amount to decrement. Default: 1
     *
     * @return integer Returns the new value of the key
     */
    public function decrement ($key, $offset = 1);

    // Setters and getters
    public function set_distribution ($type);
    public function get_distribution ();

    public function set_binary_protocol ($toggle);
    public function get_binary_protocol ();

    public function set_compression ($toggle);
    public function get_compression ();

}
