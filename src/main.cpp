#include <iostream>

// Forward declaration for the integrated pipeline
void run_complete_hft_demonstration();

int main() {
    try {
        std::cout << "🚀 HFT Trading Engine v2.0.0\n";
        std::cout << "Integrated Pipeline Demonstration\n";
        std::cout << "=================================\n\n";
        
        // Run the complete demonstration
        run_complete_hft_demonstration();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
}
