--TEST--
Persistent connection callback exception
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
require __DIR__ . '/test_driver.inc';

class MyTestException extends Exception {}

try {
	$lite = new MemcachedLite ('my_conn', function (MemcachedLite $obj, $persistent_id) {
												throw new MyTestException ('abc123');
                                          });
	echo 'should not echo this' . PHP_EOL;
} catch (MyTestException $e) {
	var_dump ($e->getMessage () == 'abc123');
}

echo "ALL OK" . PHP_EOL;

?>
--EXPECTF--
bool(true)
ALL OK