// example/return_type.zap

fun returns_int() Int {
    var a: Int = 10;
    return a;
}

fun returns_nothing() {
    // Empty body, should be parsed as void return type
}

fun add(a: Int, b: Int) Int {
    var c: Int = a + b;
    return c;
}
