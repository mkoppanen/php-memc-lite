--TEST--
Test hashing key to server
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
require __DIR__ . '/test_driver.inc';

$lite = new MemcachedLite ();
$lite->set_distribution (MemcachedLite::DISTRIBUTION_KETAMA);

try {
	var_dump ($lite->get_server ('my_key'));
} catch (MemcachedLiteException $e) {
	echo "No servers, should fail" . PHP_EOL;
}


$lite->add_server ('127.0.0.1', 1);
$lite->add_server ('127.0.0.1', 2);
$lite->add_server ('127.0.0.1', 3);
$lite->add_server ('127.0.0.1', 4);

var_dump ($lite->get_server ('first'));
var_dump ($lite->get_server ('second'));
var_dump ($lite->get_server ('third'));

echo "ALL OK" . PHP_EOL;

?>
--EXPECT--
No servers, should fail
array(2) {
  ["host"]=>
  string(9) "127.0.0.1"
  ["port"]=>
  int(4)
}
array(2) {
  ["host"]=>
  string(9) "127.0.0.1"
  ["port"]=>
  int(3)
}
array(2) {
  ["host"]=>
  string(9) "127.0.0.1"
  ["port"]=>
  int(1)
}
ALL OK