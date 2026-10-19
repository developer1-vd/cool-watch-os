// Games: Snake and an offline-dinosaur style runner.
#include "Apps.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ui;

namespace {

void gameOverCard(os::Canvas& c, int score, int best){
	card(c, 14, 36, 100, 56, col::Surface2, 12);
	text(c, "Game over", 64, 48, Body, col::Text, textdatum_t::middle_center);
	char buf[24];
	snprintf(buf, sizeof(buf), "Score %d  Best %d", score, best);
	text(c, buf, 64, 64, Small, col::TextDim, textdatum_t::middle_center);
	text(c, "SELECT to retry", 64, 80, Small, accent().primary, textdatum_t::middle_center);
}

// ---- Snake ---------------------------------------------------------------------------
// UP turns left, DOWN turns right, like the old two-button phones.

class Snake : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		best = hal::prefGet("snakeHi", 0);
		reset();
	}

	bool showStatusBar() const override { return false; }
	bool keepScreenOn() const override { return state == State::Playing; }

	void onUpdate(uint32_t dt) override {
		if(state != State::Playing) return;
		acc += dt;
		const uint32_t stepMs = len > 20 ? 90 : 150 - len * 3;
		while(acc >= stepMs){
			acc -= stepMs;
			step();
		}
	}

	bool onKey(const os::KeyEvent& e) override {
		if(e.action != os::KeyAction::Down) return e.key != os::Key::Back;
		if(e.key == os::Key::Back) return false;
		if(state != State::Playing){
			if(e.key == os::Key::Select){
				if(state == State::Over) reset();
				state = State::Playing;
			}
			return true;
		}
		if(e.key == os::Key::Up && !turned){
			dir = (dir + 3) % 4;
			turned = true;
		}else if(e.key == os::Key::Down && !turned){
			dir = (dir + 1) % 4;
			turned = true;
		}else if(e.key == os::Key::Select){
			state = State::Ready;
		}
		return true;
	}

	void onDraw(os::Canvas& c) override {
		const auto& a = accent();
		c.drawRect(0, Top - 1, os::W, os::H - Top + 1, col::Surface2);
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", len - 3);
		text(c, buf, 4, 2, Small, col::Text);
		snprintf(buf, sizeof(buf), "Best %d", best);
		text(c, buf, 124, 2, Small, col::TextDim, textdatum_t::top_right);

		c.fillSmoothCircle(food.x * Cell + Cell / 2, Top + food.y * Cell + Cell / 2, 3, col::Red);
		for(int i = 0; i < len; i++){
			const auto& p = body[i];
			const uint16_t color = i == 0 ? a.primary : blend(a.primary, a.container, (uint8_t) (i * 160 / len));
			c.fillRoundRect(p.x * Cell + 1, Top + p.y * Cell + 1, Cell - 2, Cell - 2, 2, color);
		}
		if(state == State::Ready){
			card(c, 14, 40, 100, 44, col::Surface2, 12);
			text(c, "Snake", 64, 52, Body, col::Text, textdatum_t::middle_center);
			text(c, "SELECT to start", 64, 70, Small, col::TextDim, textdatum_t::middle_center);
		}else if(state == State::Over){
			gameOverCard(c, len - 3, best);
		}
	}

private:
	static constexpr int Cell = 8, Cols = 16, Rows = 14, Top = 16;
	struct P { int8_t x, y; };
	enum class State : uint8_t { Ready, Playing, Over };

	P body[Cols * Rows];
	int len = 3;
	int dir = 1; // 0 up, 1 right, 2 down, 3 left
	bool turned = false;
	P food{ 10, 7 };
	uint32_t acc = 0;
	int best = 0;
	State state = State::Ready;

	void reset(){
		len = 3;
		dir = 1;
		for(int i = 0; i < len; i++) body[i] = { (int8_t) (6 - i), 7 };
		placeFood();
		acc = 0;
	}

	void placeFood(){
		for(;;){
			food = { (int8_t) (rand() % Cols), (int8_t) (rand() % Rows) };
			bool clash = false;
			for(int i = 0; i < len; i++) clash |= body[i].x == food.x && body[i].y == food.y;
			if(!clash) return;
		}
	}

	void step(){
		turned = false;
		static const int dx[] = { 0, 1, 0, -1 }, dy[] = { -1, 0, 1, 0 };
		P head = { (int8_t) ((body[0].x + dx[dir] + Cols) % Cols), (int8_t) ((body[0].y + dy[dir] + Rows) % Rows) };
		for(int i = 0; i < len - 1; i++){
			if(body[i].x == head.x && body[i].y == head.y){
				state = State::Over;
				if(len - 3 > best){
					best = len - 3;
					hal::prefSet("snakeHi", best);
				}
				os::play(os::Sound::Error);
				return;
			}
		}
		const bool eat = head.x == food.x && head.y == food.y;
		if(eat && len < Cols * Rows) len++;
		for(int i = len - 1; i > 0; i--) body[i] = body[i - 1];
		body[0] = head;
		if(eat){
			os::beep(1760, 25);
			placeFood();
		}
	}
};

// ---- Dino runner ---------------------------------------------------------------------

class Dino : public os::Activity {
public:
	void onCreate(const os::Intent&) override {
		best = hal::prefGet("dinoHi", 0);
		reset();
	}

	bool showStatusBar() const override { return false; }
	bool keepScreenOn() const override { return playing; }

	void onUpdate(uint32_t dt) override {
		if(!playing || over) return;
		const float s = dt / 1000.0f;
		speed += s * 4;
		distance += speed * s;
		score = (int) (distance / 10);

		// Physics
		vy += (ducking ? 1600 : 900) * s;
		y += vy * s;
		if(y >= 0){
			y = 0;
			vy = 0;
		}

		for(auto& o : obstacles){
			o.x -= speed * s;
			if(o.x < -20) spawn(o);
		}
		cloudX -= speed * s * 0.2f;
		if(cloudX < -30) cloudX = 140;

		// Collision (dino box)
		const int dh = ducking && y == 0 ? 10 : 18;
		const int dTop = Ground - dh + (int) y, dBottom = Ground + (int) y;
		for(auto& o : obstacles){
			const int ox0 = (int) o.x, ox1 = ox0 + o.w;
			const int oy0 = o.bird ? Ground - 22 : Ground - o.h;
			const int oy1 = o.bird ? Ground - 14 : Ground;
			if(ox1 > DinoX + 3 && ox0 < DinoX + 14 && oy1 > dTop + 2 && oy0 < dBottom){
				over = true;
				if(score > best){
					best = score;
					hal::prefSet("dinoHi", best);
				}
				os::play(os::Sound::Error);
			}
		}
		if(score / 100 != lastHundred){
			lastHundred = score / 100;
			os::beep(1568, 40);
		}
	}

	bool onKey(const os::KeyEvent& e) override {
		if(e.key == os::Key::Back) return false;
		if(e.key == os::Key::Down){
			ducking = e.action == os::KeyAction::Down || (ducking && e.action != os::KeyAction::Up);
			return true;
		}
		if(e.action != os::KeyAction::Down) return true;
		if(over){
			reset();
			playing = true;
			return true;
		}
		playing = true;
		if(y == 0){
			vy = -330;
			os::beep(1200, 12);
		}
		return true;
	}

	void onDraw(os::Canvas& c) override {
		const uint16_t fg = col::TextDim;
		c.drawFastHLine(0, Ground, os::W, fg);
		for(int i = 0; i < 8; i++){
			const int gx = (int) (i * 23 - fmodf(distance, 23.0f));
			c.drawFastHLine(gx, Ground + 3 + (i % 3), 3, col::TextFaint);
		}
		// Cloud
		c.fillSmoothRoundRect((int) cloudX, 28, 24, 7, 3, col::Surface2);
		c.fillSmoothCircle((int) cloudX + 10, 27, 5, col::Surface2);

		drawDino(c, DinoX, Ground + (int) y);

		for(auto& o : obstacles){
			if(o.bird){
				const int bx = (int) o.x, by = Ground - 18;
				const bool flap = ((int) distance / 15) % 2;
				c.fillRect(bx + 2, by, 12, 4, fg);
				c.fillTriangle(bx + 4, by, bx + 10, by, bx + 7, flap ? by - 6 : by + 8, fg);
				c.fillRect(bx, by + 1, 3, 2, fg);
			}else{
				const int ox = (int) o.x;
				c.fillRoundRect(ox + o.w / 2 - 2, Ground - o.h, 5, o.h, 2, col::AndroidGreen);
				c.fillRoundRect(ox, Ground - o.h + 4, 3, 6, 1, col::AndroidGreen);
				c.fillRoundRect(ox + o.w - 3, Ground - o.h + 2, 3, 6, 1, col::AndroidGreen);
			}
		}

		char buf[24];
		snprintf(buf, sizeof(buf), "HI %05d  %05d", best, score);
		text(c, buf, 124, 4, Small, col::TextDim, textdatum_t::top_right);

		if(!playing){
			text(c, "SELECT to jump", 64, 108, Small, accent().primary, textdatum_t::middle_center);
			text(c, "hold DOWN to duck", 64, 119, Small, col::TextFaint, textdatum_t::middle_center);
		}
		if(over) gameOverCard(c, score, best);
	}

private:
	static constexpr int Ground = 90, DinoX = 14;
	struct Obstacle {
		float x;
		int w, h;
		bool bird;
	};

	Obstacle obstacles[3];
	float y = 0, vy = 0, speed = 60, distance = 0, cloudX = 100;
	bool playing = false, over = false, ducking = false;
	int score = 0, best = 0, lastHundred = 0;

	void reset(){
		y = vy = distance = 0;
		speed = 60;
		over = false;
		score = lastHundred = 0;
		float x = 150;
		for(auto& o : obstacles){
			o.x = x;
			spawn(o, false);
			o.x = x;
			x += 70 + rand() % 50;
		}
	}

	void spawn(Obstacle& o, bool reposition = true){
		if(reposition){
			float far = 0;
			for(auto& other : obstacles) if(other.x > far) far = other.x;
			o.x = (far < os::W ? os::W : far) + 55 + rand() % 60 + speed * 0.3f;
		}
		o.bird = score > 150 && rand() % 4 == 0;
		o.w = o.bird ? 14 : 8 + rand() % 6;
		o.h = o.bird ? 8 : 12 + rand() % 10;
	}

	void drawDino(os::Canvas& c, int x, int base){
		const uint16_t fg = col::Text;
		const bool duck = ducking && y == 0;
		const bool step = ((int) distance / 6) % 2;
		if(duck){
			c.fillRect(x, base - 10, 18, 7, fg);
			c.fillRect(x + 14, base - 12, 8, 6, fg);
			c.fillRect(x + 18, base - 11, 1, 1, col::Bg);
		}else{
			c.fillRect(x + 6, base - 20, 10, 7, fg);       // head
			c.fillRect(x + 8, base - 19, 1, 1, col::Bg);   // eye
			c.fillRect(x + 12, base - 15, 4, 1, col::Bg);  // mouth
			c.fillRect(x + 2, base - 14, 9, 9, fg);        // body
			c.fillRect(x - 1, base - 13, 3, 4, fg);        // tail
			c.fillRect(x + 11, base - 12, 2, 2, fg);       // arm
		}
		if(!playing || y < 0){
			c.fillRect(x + 3, base - 5, 2, 5, fg);
			c.fillRect(x + 8, base - 5, 2, 5, fg);
		}else{
			c.fillRect(x + 3, base - 5, 2, step ? 5 : 3, fg);
			c.fillRect(x + 8, base - 5, 2, step ? 3 : 5, fg);
		}
	}
};

}

const os::AppInfo apps::Snake = { "snake", "Snake", col::AndroidGreen, icon::snake, os::make<::Snake>, true };
const os::AppInfo apps::Dino = { "dino", "Dino run", col::TextDim, icon::dino, os::make<::Dino>, true };
