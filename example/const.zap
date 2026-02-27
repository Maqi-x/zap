// Global constants
const PI: Float = 3.14159;
const APP_NAME: String = "Zap Demo";
const MAX_RETRIES: Int = 5;

fun main() {
    // Local constants
    const LOCAL_LIMIT: Int = 100;
    
    println(APP_NAME);
    
    println("Global PI value:");
    printFloat(PI);
    
    println("Local limit:");
    printInt(LOCAL_LIMIT);
    
    var current_retry: Int = 0;
    while (current_retry < MAX_RETRIES) {
        println("Retry attempt...");
        current_retry = current_retry + 1;
    }
    
    // The following would cause a compiler error:
    // PI = 3.14; 
    // LOCAL_LIMIT = 200;

    return 0;
}
