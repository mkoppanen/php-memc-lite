--TEST--
Persistent connection callback invocation
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
require __DIR__ . '/test_driver.inc';

$persistent_id = 'my_conn';

$lite = new MemcachedLite ($persistent_id, function (MemcachedLite $obj, $id) use ($persistent_id) {
											$obj->set_binary_protocol (true);
											$obj->add_server (TEST_MEMCACHED_HOST);
											echo 'invoked once' . PHP_EOL;

											if ($id !== $persistent_id)
												echo 'Failed' . PHP_EOL;
                                          });

$lite = new MemcachedLite ($persistent_id, function (MemcachedLite $obj, $id) use ($persistent_id) {
											$obj->set_binary_protocol (true);
											$obj->add_server (TEST_MEMCACHED_HOST);
											echo 'invoked twice' . PHP_EOL;

											if ($id !== $persistent_id)
												echo 'Failed' . PHP_EOL;
                                          });

printf ("%d server" . PHP_EOL, count ($lite->get_servers ()));
echo "ALL OK" . PHP_EOL;

?>
--EXPECTF--
invoked once
1 server
ALL OK
