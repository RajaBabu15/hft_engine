"""
HFT Trading Engine Python Package
High-performance, low-latency trading engine with Python bindings
"""

try:
    import hft_engine_cpp
    __version__ = getattr(hft_engine_cpp, '__version__', '2.0.0')
    __all__ = [
        'hft_engine_cpp',
        'Side', 'OrderType', 'OrderStatus',
        'Order', 'OrderBook', 'PriceLevel',
        'HighResolutionClock', 'LockFreeQueue',
        'IntLockFreeQueue', 'DoubleLockFreeQueue'
    ]
    
    # Re-export main classes for convenience
    from hft_engine_cpp import (
        Side, OrderType, OrderStatus,
        Order, OrderBook, PriceLevel,
        HighResolutionClock, LockFreeQueue,
        IntLockFreeQueue, DoubleLockFreeQueue
    )
    
except ImportError:
    __version__ = '2.0.0'
    __all__ = []