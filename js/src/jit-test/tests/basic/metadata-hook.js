
x = [1,2,3];
setObjectMetadata(x, {y:0});
assertEq(getObjectMetadata(x).y, 0);

incallback = false;
count = 0;

function callback(obj) {
    if (incallback)
	return null;
    incallback = true;
    var res =
      {
        count: ++count,
        location: Error().stack,
        message: Error().message // no .message => Error.prototype.message => ""
      };
    incallback = false;
    return res;
}
callback({});

setObjectMetadataCallback(callback);

function Foo() {
  this.x = 0;
  this.y = 1;
}

function f() {
  w = new Foo();
  x = [1,2,3];
  y = [2,3,5];
  z = {a:0,b:1};
}
f();

var wc = getObjectMetadata(w).count;
var xc = getObjectMetadata(x).count;
var yc = getObjectMetadata(y).count;
var zc = getObjectMetadata(z).count;

assertEq(xc > wc, true);
assertEq(yc > xc, true);
assertEq(zc > yc, true);
assertEq(/\.js/.test(getObjectMetadata(x).location), true);
assertEq(getObjectMetadata(x).message, "");
