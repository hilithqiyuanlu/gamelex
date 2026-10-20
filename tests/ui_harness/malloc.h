#pragma once
struct mallinfo { unsigned long uordblks; };
inline struct mallinfo mallinfo() { return {1024}; }
