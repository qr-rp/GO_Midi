#pragma once

// Logging disabled for lightweight build
// Use do-while to avoid expression evaluation
#define LOG(msg) do { (void)sizeof(msg); } while(0)
