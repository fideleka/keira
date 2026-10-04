#pragma once
namespace lilka {
class Canvas;
} // namespace lilka
namespace screenshot {
void request();
// Copy completed game frame before double-buffer swap; false means busy/OOM.
bool request(lilka::Canvas* gameFrame);
} // namespace screenshot
