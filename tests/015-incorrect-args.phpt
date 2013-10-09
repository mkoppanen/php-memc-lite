--TEST--
Test incorrect arguments to constructor
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
require __DIR__ . '/test_driver.inc';

try {
	$lite = new MemcachedLite (null, 'xx');
} catch (MemcachedLiteException $e) {
	echo "Failed as expected" . PHP_EOL;
}

echo "ALL OK" . PHP_EOL;

?>
--EXPECT--
Failed as expected
ALL OK