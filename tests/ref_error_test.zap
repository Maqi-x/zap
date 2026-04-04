fun increment(ref x: Int) {
    x = x + 1;
}

fun noRef(x: Int) {}

fun main() Int {
    var a: Int = 10;
    
    // Error: missing 'ref' at call site
    // increment(a); 
    
    // Error: 'ref' for non-ref parameter
    // noRef(ref a);
    
    // Error: non-l-value (literal)
    // increment(ref 10);
    
    // Error: constant passed by ref
    // const C: Int = 10;
    // increment(ref C);
    
    // Error: aliasing
    // fun swap(ref x: Int, ref y: Int) {}
    // swap(ref a, ref a);
    
    return 0;
}
