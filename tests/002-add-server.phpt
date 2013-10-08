--TEST--
Add server(s)
--SKIPIF--
<?php require_once(dirname(__FILE__) . '/skipif.inc'); ?>
--FILE--
<?php
$memc = new MemcachedLite ();

try {
	$memc->add_server ('localhost', 11211);
	$memc->add_server ('127.0.0.1', 11211);
} catch (Exception $e) {
	echo "fail";
}

var_dump ($memc->get_servers ());
echo "OK" . PHP_EOL;

?>
--EXPECT--
array(2) {
  [0]=>
  array(2) {
    ["host"]=>
    string(9) "localhost"
    ["port"]=>
    int(11211)
  }
  [1]=>
  array(2) {
    ["host"]=>
    string(9) "127.0.0.1"
    ["port"]=>
    int(11211)
  }
}
OK