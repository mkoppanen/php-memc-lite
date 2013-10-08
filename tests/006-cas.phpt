--TEST--
CAS tokens
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$exists = null;
$cas = -1;

$memc = new MemcachedLite ();
$memc->add_server ('127.0.0.1', 11211);

$memc->set ('hello_world', 'hi', 10);

// The key should exist and $cas should be greater than zero
$memc->get ('hello_world', $exists, $cas);
var_dump ($exists, $cas);

// This should not throw exception, correct cas token
$memc->set ('hello_world', 'hi', 10, $cas);

// This should not exist and zero cas returned
$memc->get ('asdaddasdasdasdsadsasdsd_' . uniqid (), $exists, $cas);
var_dump ($exists, $cas);

try {
	// Setting this key should fail because of wrong cas
	$memc->set ('hello_world', 'hi', 10, 1);
} catch (Exception $e) {
	echo "CAS failed as expected" . PHP_EOL;
}

echo "OK" . PHP_EOL;

?>
--EXPECTF--
bool(true)
string(8) %s
bool(false)
NULL
CAS failed as expected
OK