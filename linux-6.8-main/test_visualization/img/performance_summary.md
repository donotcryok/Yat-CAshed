# Yat-CAShed Performance Test Results

## Test Configuration
- Number of threads: 8
- Test type: Cache-intensive workload
- Cache size: 2MB per thread
- Iterations: 10 per thread
- Schedulers compared: CFS vs Yat-CAShed

## CPU Switches Analysis

- CFS: 16.0 (avg)
- Yat-CAShed: 2.2 (avg)
- **Reduction: 85.9%**

## Execution Time Analysis
- CFS: 1.161s (avg)
- Yat-CAShed: 1.151s (avg)
- **Performance improvement: 0.9%**

## CPU Affinity Analysis
- CFS: 0.522 (avg)
- Yat-CAShed: 0.744 (avg)
- **Improvement: 42.4%**

## Conclusion
Yat-CAShed shows clear advantages in reducing CPU migrations and improving performance.