--TEST--
CAS tokens
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$exists = null;

$memc = new MemcachedLite ();
$memc->add_server ('127.0.0.1', 11211);

$memc->set ('hello_world', 'hi', 10);

$cas = -1;
$memc->get ('hello_world', $exists, $cas);
var_dump ($exists, $cas > 0);

$memc->set ('hello_world', 'hi', 10, $cas);

$memc->get ('asdaddasdasdasdsadsasdsd', $exists, $cas);
var_dump ($exists, $cas);

try {
	$memc->set ('hello_world', 'hi', 10, 1);
} catch (Exception $e) {
	echo "CAS failed as expected" . PHP_EOL;
}

echo "OK" . PHP_EOL;

?>
--EXPECT--
bool(true)
bool(true)
bool(false)
int(0)
CAS failed as expected
OK