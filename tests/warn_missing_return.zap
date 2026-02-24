fun foo() Int {
    var x: Int = 1;
    // no return here - should trigger a warning
}

fun main() Int {
    foo();
    return 0;
}
