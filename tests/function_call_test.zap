fun add(a: Int, b: Int) Int {
    return a + b;
}

fun factorial(n: Int) Int {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

fun is_even(n: Int) Bool {
    if n == 0 {
        return true;
    }
    return is_odd(n - 1);
}

fun is_odd(n: Int) Bool {
    if n == 0 {
        return false;
    }
    return is_even(n - 1);
}

fun main() Int {
    var a: Int = add(10, 20);
    if a != 30 { return 0; }
    
    var f: Int = factorial(5); // 120
    if f != 120 { return 0; }
    
    if !is_even(4) { return 0; }
    if is_even(5) { return 0; }
    
    return 1; // All tests passed
}
