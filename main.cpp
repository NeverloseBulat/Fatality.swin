#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <chrono>
#include <deque>
#include <functional>

using namespace geode::prelude;

// =====================================================================
//  Состояние и настройки
// =====================================================================
enum Opt : int {
	O_JUMP, O_BUNNY,
	O_NOCLIP, O_NOSPIKES, O_GOD, O_REVGRAV, O_HIDEATT,
	O_LDM, O_AUTOLDM, O_HITBOX, O_NOWAVE,
	O_SPIN, O_AA, O_FLIPBACK, O_FLIPDOWN, O_SHAKE, O_NLGRAV, O_SPEEDHACK,
	O_CHEATIND, O_CPS, O_TIME, O_FPS, O_AUTORETRY,
	O_SHOWPCT, O_HIDEPLAYER, O_SLOW, O_FAST, O_NODEATHFX,
	O_BRANDING, O_BLUR, O_RAINBOW, O_GLOW, O_CHARACTER, O_INTRO,
	O_COUNT
};

struct Def { char const* key; char const* label; bool def; };
static Def const DEFS[O_COUNT] = {
	{"jump", "Jump Hack", false}, {"bunny", "Bunny Hop", false},
	{"noclip", "Noclip", false}, {"nospikes", "No Spikes", false}, {"god", "God Mode", false},
	{"revgrav", "Reverse Grav", false}, {"hideatt", "Hide Attempts", false},
	{"ldm", "LDM", false}, {"autoldm", "Auto LDM", false}, {"hitbox", "Hitboxes", false}, {"nowave", "No Wave Trail", false},
	{"spin", "Spinbot", false}, {"aa", "Anti-Aim", false}, {"flipback", "Flip Back", false},
	{"flipdown", "Flip Down", false}, {"shake", "Shake", false}, {"nlgrav", "Flip Cube", false}, {"speedhack", "Speedhack", false},
	{"cheatind", "Cheat Indicator", false}, {"cps", "CPS", false}, {"time", "Time", false}, {"fps", "FPS", false},
	{"autoretry", "Auto Retry", false},
	{"showpct", "Show %", false}, {"hideplayer", "Hide Player", false}, {"slow", "Slow Motion", false},
	{"fast", "Fast Motion", false}, {"nodeathfx", "No Death FX", false},
	{"branding", "Branding", true}, {"blur", "Blur BG", true}, {"rainbow", "Rainbow", false},
	{"glow", "Glow Cube", false}, {"character", "Character", false}, {"intro", "Intro", true},
};

static char const* const COLOR_NAMES[6] = {"Dark", "Blue", "Pink", "Green", "Red", "Purple"};
static int const COLOR_PRESETS[6][3] = {
	{30, 30, 40}, {35, 55, 130}, {140, 40, 100}, {35, 100, 55}, {130, 35, 35}, {85, 40, 130}
};
static char const* const OPACITY_NAMES[3] = {"50%", "75%", "100%"};
static int const OPACITY_VALUES[3] = {127, 191, 255};
static char const* const SCALE_NAMES[3] = {"Small", "Normal", "Big"};
static float const SCALE_VALUES[3] = {0.8f, 1.0f, 1.2f};
static char const* const BTN_NAMES[6] = {"White", "Cyan", "Pink", "Green", "Orange", "Red"};
static ccColor3B const BTN_COLORS[6] = {
	{255, 255, 255}, {80, 220, 255}, {255, 110, 200}, {110, 255, 130}, {255, 170, 60}, {255, 80, 80}
};

namespace S {
	inline bool tog[O_COUNT];
	inline float speed = 1.f;       // 0.1 .. 5
	inline float spinSpeed = 20.f;  // -60 .. 60
	inline float cubeSize = 1.f;    // 1 .. 10
	inline int col[3] = {30, 30, 40};
	inline int cyc[4] = {0, 2, 1, 0}; // цвет, прозрачность, размер, цвет кнопок
	inline bool pendingInstant = false;
	inline float realDt = 1.f / 60.f;
	inline float fps = 60.f;
	inline std::deque<double> clicks;

	inline bool t(int i) { return tog[i]; }

	inline void load() {
		auto m = Mod::get();
		for (int i = 0; i < O_COUNT; i++) tog[i] = m->getSavedValue<bool>(DEFS[i].key, DEFS[i].def);
		speed = static_cast<float>(m->getSavedValue<double>("speed", 1.0));
		spinSpeed = static_cast<float>(m->getSavedValue<double>("spin-speed", 20.0));
		cubeSize = static_cast<float>(m->getSavedValue<double>("cube-size", 1.0));
		col[0] = m->getSavedValue<int>("col-r", 30);
		col[1] = m->getSavedValue<int>("col-g", 30);
		col[2] = m->getSavedValue<int>("col-b", 40);
		for (int i = 0; i < 4; i++) cyc[i] = m->getSavedValue<int>(fmt::format("cyc{}", i), cyc[i]);
	}
	inline void save() {
		auto m = Mod::get();
		for (int i = 0; i < O_COUNT; i++) m->setSavedValue<bool>(DEFS[i].key, tog[i]);
		m->setSavedValue<double>("speed", speed);
		m->setSavedValue<double>("spin-speed", spinSpeed);
		m->setSavedValue<double>("cube-size", cubeSize);
		m->setSavedValue<int>("col-r", col[0]);
		m->setSavedValue<int>("col-g", col[1]);
		m->setSavedValue<int>("col-b", col[2]);
		for (int i = 0; i < 4; i++) m->setSavedValue<int>(fmt::format("cyc{}", i), cyc[i]);
	}
	inline bool anyCheat() {
		return tog[O_JUMP] || tog[O_BUNNY] || tog[O_NOCLIP] || tog[O_NOSPIKES] || tog[O_GOD] || tog[O_REVGRAV] ||
			(tog[O_SPEEDHACK] && std::abs(speed - 1.f) > 0.01f) || tog[O_SLOW] || tog[O_FAST];
	}
	inline double now() {
		using namespace std::chrono;
		return duration<double>(steady_clock::now().time_since_epoch()).count();
	}
	inline float effectiveSpeed() {
		float m = tog[O_SPEEDHACK] ? speed : 1.f;
		if (tog[O_SLOW]) m *= 0.5f;
		if (tog[O_FAST]) m *= 2.f;
		return m;
	}
}

$on_mod(Loaded) { S::load(); }

// =====================================================================
//  Вспомогательное
// =====================================================================
static CCSprite* makeSprite(std::string const& frame) {
	if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame.c_str())) return nullptr;
	return CCSprite::createWithSpriteFrameName(frame.c_str());
}

static void tint(CCNode* node, ccColor3B c) {
	if (auto rgb = typeinfo_cast<CCRGBAProtocol*>(node)) rgb->setColor(c);
	if (auto kids = node->getChildren()) {
		for (auto kid : CCArrayExt<CCNode*>(kids)) tint(kid, c);
	}
}

static ccColor3B hsv(float h, float s, float v) {
	h = h - std::floor(h);
	int i = static_cast<int>(h * 6);
	float f = h * 6 - i;
	float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
	float r, g, b;
	switch (i % 6) {
		case 0: r = v; g = t; b = p; break;
		case 1: r = q; g = v; b = p; break;
		case 2: r = p; g = v; b = t; break;
		case 3: r = p; g = q; b = v; break;
		case 4: r = t; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
	}
	return {static_cast<GLubyte>(r * 255), static_cast<GLubyte>(g * 255), static_cast<GLubyte>(b * 255)};
}

static void applyHitboxSetting() {
	GameManager::get()->setGameVariable("0166", S::t(O_HITBOX));
	if (auto pl = PlayLayer::get()) pl->updateDebugDrawSettings();
}

// =====================================================================
//  Стикер (перетаскиваемый)
// =====================================================================
class Sticker : public CCLayer {
public:
	std::string m_frame;
	float m_scale = 1.f;
	bool m_editable = false;
	CCSprite* m_spr = nullptr;
	CCPoint m_grab = {0, 0};
	std::function<void(Sticker*)> m_onSelect;
	std::function<void(Sticker*)> m_onChanged;

	static Sticker* create(std::string const& frame, CCPoint pos, float scale) {
		auto ret = new Sticker();
		if (ret && ret->init2(frame, pos, scale)) {
			ret->autorelease();
			return ret;
		}
		CC_SAFE_DELETE(ret);
		return nullptr;
	}

	bool init2(std::string const& frame, CCPoint pos, float scale) {
		if (!CCLayer::init()) return false;
		m_spr = makeSprite(frame);
		if (!m_spr) return false;
		m_frame = frame;
		m_scale = scale;
		auto sz = m_spr->getContentSize();
		this->ignoreAnchorPointForPosition(false);
		this->setAnchorPoint({0.5f, 0.5f});
		this->setContentSize(sz);
		m_spr->setPosition({sz.width / 2, sz.height / 2});
		this->addChild(m_spr);
		this->setScale(scale);
		this->setPosition(pos);
		this->setTouchEnabled(true);
		return true;
	}

	void setSelected(bool v) {
		m_spr->setColor(v ? ccc3(255, 240, 120) : ccc3(255, 255, 255));
	}

	void registerWithTouchDispatcher() override {
		CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -510, true);
	}

	bool ccTouchBegan(CCTouch* t, CCEvent*) override {
		if (!m_editable || !this->getParent()) return false;
		auto p = this->getParent()->convertTouchToNodeSpace(t);
		if (!this->boundingBox().containsPoint(p)) return false;
		m_grab = ccpSub(this->getPosition(), p);
		if (m_onSelect) m_onSelect(this);
		return true;
	}
	void ccTouchMoved(CCTouch* t, CCEvent*) override {
		auto p = this->getParent()->convertTouchToNodeSpace(t);
		this->setPosition(ccpAdd(p, m_grab));
	}
	void ccTouchEnded(CCTouch*, CCEvent*) override {
		if (m_onChanged) m_onChanged(this);
	}
	void ccTouchCancelled(CCTouch*, CCEvent*) override {
		if (m_onChanged) m_onChanged(this);
	}
};

static std::vector<std::string> const STICKER_FRAMES = {
	"GJ_starsIcon_001.png",
	"GJ_moonsIcon_001.png",
	"GJ_diamondsIcon_001.png",
	"GJ_coinsIcon_001.png",
	"GJ_likesIcon_001.png",
	"GJ_heartOn_001.png",
	"GJ_downloadsIcon_001.png",
	"currencyOrbIcon_001.png",
};

// =====================================================================
//  Главное меню мода
// =====================================================================
class FatalityMenu : public Popup {
protected:
	int m_tab = 0;
	CCNode* m_content = nullptr;
	CCMenu* m_contentMenu = nullptr;
	CCMenu* m_tabMenu = nullptr;
	CCLayerColor* m_dim = nullptr;
	std::vector<CCMenuItemSpriteExtra*> m_tabBtns;
	std::vector<CCMenuItemSpriteExtra*> m_cycleBtns;
	CCMenuItemToggler* m_togglers[O_COUNT] = {};
	std::vector<Sticker*> m_stickers;
	Sticker* m_selected = nullptr;
	SimplePlayer* m_char = nullptr;
	CCLabelBMFont* m_speedLabel = nullptr;
	CCLabelBMFont* m_spinLabel = nullptr;
	CCLabelBMFont* m_cubeLabel = nullptr;
	float m_hue = 0.f;

	static constexpr float X0 = 120.f;
	static constexpr char const* TAB_NAMES[8] = {"Main", "Rage", "Visuals", "Anti-Aim", "Legit", "QOL", "World", "Decor"};

	float rowY(int i) { return m_size.height - 62.f - i * 30.f; }

	bool init() {
		if (!Popup::init(440.f, 300.f)) return false;
		this->setTitle("Fatality.win");

		m_dim = CCLayerColor::create({0, 0, 0, 0});
		this->addChild(m_dim, -1);

		m_tabMenu = CCMenu::create();
		m_tabMenu->setPosition({0, 0});
		m_mainLayer->addChild(m_tabMenu, 5);

		for (int i = 0; i < 8; i++) {
			auto spr = ButtonSprite::create(TAB_NAMES[i], 70, true, "bigFont.fnt", "GJ_button_01.png", 26.f, 0.55f);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FatalityMenu::onTab));
			btn->setTag(i);
			btn->setPosition({58.f, m_size.height - 52.f - i * 29.f});
			m_tabMenu->addChild(btn);
			m_tabBtns.push_back(btn);
		}

		auto line = CCLayerColor::create({255, 255, 255, 50}, 1.5f, m_size.height - 70.f);
		line->setPosition({104.f, 16.f});
		m_mainLayer->addChild(line, 4);

		m_content = CCNode::create();
		m_mainLayer->addChild(m_content, 6);

		this->loadStickers();
		this->buildTab(0);
		this->applyTheme();
		this->schedule(schedule_selector(FatalityMenu::onRainbow), 0.15f);
		return true;
	}

	// ------------------------------------------------ тема / внешний вид
	void applyTheme() {
		m_bgSprite->setColor({
			static_cast<GLubyte>(S::col[0]), static_cast<GLubyte>(S::col[1]), static_cast<GLubyte>(S::col[2])
		});
		m_bgSprite->setOpacity(static_cast<GLubyte>(OPACITY_VALUES[std::clamp(S::cyc[1], 0, 2)]));
		m_mainLayer->setScale(SCALE_VALUES[std::clamp(S::cyc[2], 0, 2)]);
		if (m_title) m_title->setVisible(S::t(O_BRANDING));
		m_dim->setOpacity(S::t(O_BLUR) ? 110 : 0);

		auto bc = BTN_COLORS[std::clamp(S::cyc[3], 0, 5)];
		for (size_t i = 0; i < m_tabBtns.size(); i++) {
			bool sel = static_cast<int>(i) == m_tab;
			tint(m_tabBtns[i]->getNormalImage(), sel ? bc : ccc3(110, 110, 110));
		}
		for (auto b : m_cycleBtns) tint(b->getNormalImage(), bc);

		// плавающий кубик-персонаж
		if (S::t(O_CHARACTER) && !m_char) {
			auto gm = GameManager::get();
			m_char = SimplePlayer::create(gm->getPlayerFrame());
			m_char->setColors(gm->colorForIdx(gm->getPlayerColor()), gm->colorForIdx(gm->getPlayerColor2()));
			m_char->setPosition({m_size.width - 38.f, 40.f});
			m_char->setScale(0.9f);
			auto up = CCEaseSineInOut::create(CCMoveBy::create(0.9f, {0, 10}));
			auto down = CCEaseSineInOut::create(CCMoveBy::create(0.9f, {0, -10}));
			m_char->runAction(CCRepeatForever::create(CCSequence::create(up, down, nullptr)));
			m_mainLayer->addChild(m_char, 8);
		} else if (!S::t(O_CHARACTER) && m_char) {
			m_char->removeFromParentAndCleanup(true);
			m_char = nullptr;
		}
	}

	void onRainbow(float) {
		if (!S::t(O_RAINBOW)) return;
		m_hue += 0.03f;
		auto c = hsv(m_hue, 0.7f, 0.55f);
		S::col[0] = c.r; S::col[1] = c.g; S::col[2] = c.b;
		this->applyTheme();
	}

	// перетаскивание меню за любое свободное место
	bool ccTouchBegan(CCTouch* t, CCEvent* e) override {
		return Popup::ccTouchBegan(t, e);
	}
	void ccTouchMoved(CCTouch* t, CCEvent*) override {
		auto d = ccpSub(t->getLocation(), t->getPreviousLocation());
		m_mainLayer->setPosition(ccpAdd(m_mainLayer->getPosition(), d));
	}

	// ------------------------------------------------ вкладки
	void onTab(CCObject* s) {
		this->buildTab(static_cast<CCNode*>(s)->getTag());
	}

	void buildTab(int tab) {
		m_tab = tab;
		m_content->removeAllChildrenWithCleanup(true);
		m_cycleBtns.clear();
		for (auto& t : m_togglers) t = nullptr;
		m_speedLabel = m_spinLabel = m_cubeLabel = nullptr;

		m_contentMenu = CCMenu::create();
		m_contentMenu->setPosition({0, 0});
		m_content->addChild(m_contentMenu, 2);

		for (auto s : m_stickers) s->m_editable = (tab == 7);
		if (tab != 7) this->select(nullptr);

		switch (tab) {
			case 0: grid({O_JUMP, O_BUNNY}, 0); break;
			case 1: grid({O_NOCLIP, O_NOSPIKES, O_GOD, O_REVGRAV, O_HIDEATT, 1000}, 0); break;
			case 2: grid({O_LDM, O_AUTOLDM, O_HITBOX, O_NOWAVE}, 0); break;
			case 3:
				grid({O_SPIN, O_AA, O_FLIPBACK, O_FLIPDOWN, O_SHAKE, O_NLGRAV, O_SPEEDHACK}, 0);
				addLabel(4, "Spin speed");
				addSlider(4, (S::spinSpeed + 60.f) / 120.f, menu_selector(FatalityMenu::onSpinSlider), &m_spinLabel,
					fmt::format("{:.0f}", S::spinSpeed));
				addLabel(5, "Speed");
				addSlider(5, (S::speed - 0.1f) / 4.9f, menu_selector(FatalityMenu::onSpeedSlider), &m_speedLabel,
					fmt::format("{:.2f}x", S::speed));
				break;
			case 4: grid({O_CHEATIND, O_CPS, O_TIME, O_FPS, O_AUTORETRY}, 0); break;
			case 5: grid({O_SHOWPCT, O_HIDEPLAYER, O_SLOW, O_FAST, O_NODEATHFX}, 0); break;
			case 6:
				grid({2000, 2001, 2002, 2003, O_BRANDING, O_BLUR, O_RAINBOW, O_GLOW, O_CHARACTER, O_INTRO}, 0);
				addLabel(5, "Cube size");
				addSlider(5, (S::cubeSize - 1.f) / 9.f, menu_selector(FatalityMenu::onCubeSlider), &m_cubeLabel,
					fmt::format("{:.2f}", S::cubeSize));
				break;
			case 7: buildDecor(); break;
		}
		this->applyTheme();
	}

	// ------------------------------------------------ элементы интерфейса
	void addLabel(int row, char const* text, float x = X0) {
		auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
		lbl->setScale(0.35f);
		lbl->setAnchorPoint({0, 0.5f});
		lbl->setPosition({x, rowY(row)});
		m_content->addChild(lbl);
	}

	std::string cycleText(int k) {
		switch (k) {
			case 0: return fmt::format("Color: {}", COLOR_NAMES[std::clamp(S::cyc[0], 0, 5)]);
			case 1: return fmt::format("Opacity: {}", OPACITY_NAMES[std::clamp(S::cyc[1], 0, 2)]);
			case 2: return fmt::format("Scale: {}", SCALE_NAMES[std::clamp(S::cyc[2], 0, 2)]);
			default: return fmt::format("Btn: {}", BTN_NAMES[std::clamp(S::cyc[3], 0, 5)]);
		}
	}

	// сетка из 2 колонок: id < O_COUNT = переключатель, 1000 = Instant, 2000+ = циклическая кнопка
	void grid(std::vector<int> ids, int startRow) {
		for (size_t i = 0; i < ids.size(); i++) {
			int col = static_cast<int>(i % 2);
			int row = startRow + static_cast<int>(i / 2);
			float lx = col == 0 ? 120.f : 282.f;
			float y = rowY(row);
			int id = ids[i];
			if (id < O_COUNT) {
				addLabel(row, DEFS[id].label, lx);
				auto t = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(FatalityMenu::onToggle), 0.6f);
				t->setTag(id);
				t->toggle(S::tog[id]);
				t->setPosition({lx + 138.f, y});
				m_contentMenu->addChild(t);
				m_togglers[id] = t;
			} else if (id == 1000) {
				auto spr = ButtonSprite::create("Instant", 100, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.45f);
				auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FatalityMenu::onInstant));
				btn->setPosition({lx + 66.f, y});
				m_contentMenu->addChild(btn);
				m_cycleBtns.push_back(btn);
			} else {
				int k = id - 2000;
				auto spr = ButtonSprite::create(cycleText(k).c_str(), 120, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.4f);
				auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FatalityMenu::onCycle));
				btn->setTag(k);
				btn->setPosition({lx + 70.f, y});
				m_contentMenu->addChild(btn);
				m_cycleBtns.push_back(btn);
			}
		}
	}

	void addSlider(int row, float value, SEL_MenuHandler cb, CCLabelBMFont** valueLabel, std::string const& text) {
		auto slider = Slider::create(this, cb, 0.65f);
		slider->setPosition({X0 + 170.f, rowY(row)});
		slider->setValue(std::clamp(value, 0.f, 1.f));
		m_content->addChild(slider, 3);
		if (valueLabel) {
			auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
			l->setScale(0.35f);
			l->setPosition({m_size.width - 38.f, rowY(row)});
			m_content->addChild(l);
			*valueLabel = l;
		}
	}

	// ------------------------------------------------ обработчики
	void onToggle(CCObject* s) {
		auto t = static_cast<CCMenuItemToggler*>(s);
		int id = t->getTag();
		// колбэк вызывается до переключения, поэтому новое значение = !isToggled()
		bool on = !t->isToggled();
		S::tog[id] = on;

		// Slow и Fast взаимоисключающие
		if (on && id == O_SLOW && m_togglers[O_FAST]) { S::tog[O_FAST] = false; m_togglers[O_FAST]->toggle(false); }
		if (on && id == O_FAST && m_togglers[O_SLOW]) { S::tog[O_SLOW] = false; m_togglers[O_SLOW]->toggle(false); }

		if (id == O_HITBOX) applyHitboxSetting();
		if (id == O_AUTORETRY) GameManager::get()->setGameVariable("0026", on);

		S::save();
		this->applyTheme();
	}

	void onCycle(CCObject* s) {
		auto item = static_cast<CCMenuItemSpriteExtra*>(s);
		int k = item->getTag();
		int const mods[4] = {6, 3, 3, 6};
		S::cyc[k] = (S::cyc[k] + 1) % mods[k];
		if (k == 0) {
			for (int i = 0; i < 3; i++) S::col[i] = COLOR_PRESETS[S::cyc[0]][i];
		}
		if (auto spr = typeinfo_cast<ButtonSprite*>(item->getNormalImage())) {
			spr->setString(cycleText(k).c_str());
		}
		S::save();
		this->applyTheme();
	}

	void onInstant(CCObject*) {
		if (!PlayLayer::get()) {
			Notification::create("Open a level first", NotificationIcon::Warning)->show();
			return;
		}
		S::pendingInstant = true;
		Notification::create("Resume the game: the level will finish", NotificationIcon::Success)->show();
		this->onClose(nullptr);
	}

	void onSpeedSlider(CCObject* s) {
		S::speed = 0.1f + static_cast<SliderThumb*>(s)->getValue() * 4.9f;
		if (m_speedLabel) m_speedLabel->setString(fmt::format("{:.2f}x", S::speed).c_str());
		S::save();
	}
	void onSpinSlider(CCObject* s) {
		S::spinSpeed = static_cast<SliderThumb*>(s)->getValue() * 120.f - 60.f;
		if (m_spinLabel) m_spinLabel->setString(fmt::format("{:.0f}", S::spinSpeed).c_str());
		S::save();
	}
	void onCubeSlider(CCObject* s) {
		S::cubeSize = 1.f + static_cast<SliderThumb*>(s)->getValue() * 9.f;
		if (m_cubeLabel) m_cubeLabel->setString(fmt::format("{:.2f}", S::cubeSize).c_str());
		S::save();
	}

	// ------------------------------------------------ Decor
	void buildDecor() {
		char const* rgb[3] = {"R", "G", "B"};
		SEL_MenuHandler cbs[3] = {
			menu_selector(FatalityMenu::onColR),
			menu_selector(FatalityMenu::onColG),
			menu_selector(FatalityMenu::onColB),
		};
		for (int i = 0; i < 3; i++) {
			addLabel(i, rgb[i]);
			addSlider(i, S::col[i] / 255.f, cbs[i], nullptr, "");
		}

		auto hint = CCLabelBMFont::create("Stickers: tap to add, drag to move", "bigFont.fnt");
		hint->setScale(0.33f);
		hint->setAnchorPoint({0, 0.5f});
		hint->setPosition({X0, rowY(3) + 2.f});
		m_content->addChild(hint);

		int n = 0;
		for (auto const& name : STICKER_FRAMES) {
			auto spr = makeSprite(name);
			if (!spr) { n++; continue; }
			spr->setScale(0.6f);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(FatalityMenu::onAddSticker));
			btn->setTag(n);
			btn->setPosition({X0 + 14.f + n * 36.f, rowY(3) - 24.f});
			m_contentMenu->addChild(btn);
			n++;
		}

		struct Btn { char const* text; SEL_MenuHandler cb; float x; };
		Btn btns[3] = {
			{"Size +", menu_selector(FatalityMenu::onBigger), X0 + 45.f},
			{"Size -", menu_selector(FatalityMenu::onSmaller), X0 + 135.f},
			{"Delete", menu_selector(FatalityMenu::onDelete), X0 + 225.f},
		};
		for (auto& b : btns) {
			auto spr = ButtonSprite::create(b.text, 70, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.45f);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, b.cb);
			btn->setPosition({b.x, rowY(3) - 66.f});
			m_contentMenu->addChild(btn);
			m_cycleBtns.push_back(btn);
		}
	}

	void setCol(int i, CCObject* s) {
		S::col[i] = static_cast<int>(static_cast<SliderThumb*>(s)->getValue() * 255.f);
		S::save();
		this->applyTheme();
	}
	void onColR(CCObject* s) { setCol(0, s); }
	void onColG(CCObject* s) { setCol(1, s); }
	void onColB(CCObject* s) { setCol(2, s); }

	// ------------------------------------------------ стикеры
	void select(Sticker* s) {
		if (m_selected) m_selected->setSelected(false);
		m_selected = s;
		if (m_selected) m_selected->setSelected(true);
	}

	Sticker* spawnSticker(std::string const& frame, CCPoint pos, float scale) {
		auto st = Sticker::create(frame, pos, scale);
		if (!st) return nullptr;
		st->m_editable = (m_tab == 7);
		st->m_onSelect = [this](Sticker* x) { this->select(x); };
		st->m_onChanged = [this](Sticker*) { this->saveStickers(); };
		m_mainLayer->addChild(st, 20);
		m_stickers.push_back(st);
		return st;
	}

	void onAddSticker(CCObject* s) {
		int idx = static_cast<CCNode*>(s)->getTag();
		auto st = spawnSticker(STICKER_FRAMES[idx], {m_size.width - 70.f, 80.f}, 1.f);
		if (st) {
			this->select(st);
			this->saveStickers();
		}
	}
	void onBigger(CCObject*) {
		if (!m_selected) return;
		m_selected->m_scale = std::min(m_selected->m_scale + 0.15f, 4.f);
		m_selected->setScale(m_selected->m_scale);
		saveStickers();
	}
	void onSmaller(CCObject*) {
		if (!m_selected) return;
		m_selected->m_scale = std::max(m_selected->m_scale - 0.15f, 0.2f);
		m_selected->setScale(m_selected->m_scale);
		saveStickers();
	}
	void onDelete(CCObject*) {
		if (!m_selected) return;
		auto it = std::find(m_stickers.begin(), m_stickers.end(), m_selected);
		if (it != m_stickers.end()) m_stickers.erase(it);
		m_selected->removeFromParentAndCleanup(true);
		m_selected = nullptr;
		saveStickers();
	}

	void saveStickers() {
		std::string out;
		for (auto s : m_stickers) {
			out += fmt::format("{}|{}|{}|{};", s->m_frame, s->getPositionX(), s->getPositionY(), s->m_scale);
		}
		Mod::get()->setSavedValue<std::string>("stickers", out);
	}

	void loadStickers() {
		auto data = Mod::get()->getSavedValue<std::string>("stickers", "");
		for (auto const& item : utils::string::split(data, ";")) {
			auto parts = utils::string::split(item, "|");
			if (parts.size() != 4) continue;
			try {
				spawnSticker(parts[0], {std::stof(parts[1]), std::stof(parts[2])}, std::stof(parts[3]));
			} catch (...) {}
		}
	}

public:
	static FatalityMenu* create() {
		auto ret = new FatalityMenu();
		if (ret->init()) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

// =====================================================================
//  Кнопка открытия меню (главное меню GD и пауза)
// =====================================================================
static void addOpenButton(CCNode* layer, CCNode* menuOrNull, SEL_MenuHandler cb, CCObject* target) {
	auto spr = ButtonSprite::create("F", 30, true, "bigFont.fnt", "GJ_button_01.png", 30.f, 0.8f);
	auto btn = CCMenuItemSpriteExtra::create(spr, target, cb);
	btn->setID("fatality-button"_spr);
	if (menuOrNull) {
		menuOrNull->addChild(btn);
		menuOrNull->updateLayout();
	} else {
		auto menu = CCMenu::create();
		auto win = CCDirector::sharedDirector()->getWinSize();
		menu->setPosition({30.f, win.height - 30.f});
		menu->addChild(btn);
		layer->addChild(menu, 100);
	}
}

// Интро-анимация 5 секунд: кольца, мерцающие точки, прогресс-бар
static void buildIntro(CCNode* parent, CCObject* target, SEL_CallFuncN doneCb) {
	auto win = CCDirector::sharedDirector()->getWinSize();
	ccColor4F accent = {S::col[0] / 255.f * 0.6f + 0.4f, S::col[1] / 255.f * 0.6f + 0.4f, S::col[2] / 255.f * 0.6f + 0.4f, 1.f};

	auto layer = CCLayerColor::create({8, 6, 14, 255});
	layer->setZOrder(10000);

	auto rings = CCNode::create();
	rings->setPosition({win.width / 2, win.height / 2 + 15.f});
	layer->addChild(rings);
	for (int i = 0; i < 3; i++) {
		auto d = CCDrawNode::create();
		float r = 38.f + i * 20.f;
		for (int k = 0; k < 3; k++) { // три дуги по 80 градусов
			float a0 = k * 120.f;
			for (int s = 0; s < 20; s++) {
				float a1 = (a0 + s * 4.f) * 3.14159265f / 180.f;
				float a2 = (a0 + (s + 1) * 4.f) * 3.14159265f / 180.f;
				d->drawSegment({std::cos(a1) * r, std::sin(a1) * r}, {std::cos(a2) * r, std::sin(a2) * r}, 1.2f, accent);
			}
		}
		float dur = 3.f - i * 0.7f;
		d->runAction(CCRepeatForever::create(CCRotateBy::create(dur, (i % 2 == 0) ? 360.f : -360.f)));
		rings->addChild(d);
	}

	for (int i = 0; i < 40; i++) { // мерцающие точки
		float sz = 2.f + (std::rand() % 20) / 10.f;
		auto dot = CCLayerColor::create({static_cast<GLubyte>(accent.r * 255), static_cast<GLubyte>(accent.g * 255), static_cast<GLubyte>(accent.b * 255), 255}, sz, sz);
		dot->setPosition({static_cast<float>(std::rand() % static_cast<int>(win.width)),
			static_cast<float>(std::rand() % static_cast<int>(win.height))});
		dot->setOpacity(static_cast<GLubyte>(60 + std::rand() % 150));
		float t = 0.3f + (std::rand() % 10) / 10.f;
		dot->runAction(CCRepeatForever::create(CCSequence::create(CCFadeTo::create(t, 20), CCFadeTo::create(t, 220), nullptr)));
		layer->addChild(dot);
	}

	auto title = CCLabelBMFont::create("Fatality.win", "goldFont.fnt");
	title->setPosition({win.width / 2, win.height / 2 + 15.f});
	title->setScale(0.8f);
	layer->addChild(title, 2);

	auto barBg = CCLayerColor::create({255, 255, 255, 40}, 220.f, 5.f);
	barBg->setPosition({win.width / 2 - 110.f, 45.f});
	layer->addChild(barBg, 2);
	auto bar = CCLayerColor::create({static_cast<GLubyte>(accent.r * 255), static_cast<GLubyte>(accent.g * 255), static_cast<GLubyte>(accent.b * 255), 255}, 1.f, 5.f);
	bar->setPosition({win.width / 2 - 110.f, 45.f});
	bar->ignoreAnchorPointForPosition(false);
	bar->setAnchorPoint({0, 0});
	bar->setScaleX(0.01f);
	bar->runAction(CCScaleTo::create(5.f, 220.f, 1.f));
	layer->addChild(bar, 3);

	layer->runAction(CCSequence::create(CCDelayTime::create(5.f), CCCallFuncN::create(target, doneCb), nullptr));
	parent->addChild(layer);
}

class $modify(FatalityMenuLayer, MenuLayer) {
	bool init() {
		if (!MenuLayer::init()) return false;
		addOpenButton(this, this->getChildByID("bottom-menu"), menu_selector(FatalityMenuLayer::onFatality), this);

		static bool introShown = false;
		if (!introShown) {
			introShown = true;
			if (S::t(O_INTRO)) buildIntro(this, this, callfuncN_selector(FatalityMenuLayer::onIntroDone));
		}
		return true;
	}
	void onFatality(CCObject*) { FatalityMenu::create()->show(); }
	void onIntroDone(CCNode* n) { n->removeFromParentAndCleanup(true); }
};

class $modify(FatalityPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();
		addOpenButton(this, this->getChildByID("right-button-menu"), menu_selector(FatalityPauseLayer::onFatality), this);
	}
	void onFatality(CCObject*) { FatalityMenu::create()->show(); }
};

// =====================================================================
//  Функции в игре
// =====================================================================
class $modify(CheatPlayLayer, PlayLayer) {
	struct Fields {
		CCLabelBMFont* hud = nullptr;
		float timer = 0.f;
	};

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		// O_LDM до создания уровня
		if (level && (S::t(O_LDM) || (S::t(O_AUTOLDM) && level->m_lowDetailMode))) {
			level->m_lowDetailModeToggled = true;
		}
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}

	void setupHasCompleted() {
		PlayLayer::setupHasCompleted();

		auto win = CCDirector::sharedDirector()->getWinSize();
		auto hud = CCLabelBMFont::create("", "bigFont.fnt");
		hud->setScale(0.4f);
		hud->setAnchorPoint({0, 1});
		hud->setPosition({6.f, win.height - 6.f});
		hud->setOpacity(210);
		hud->setZOrder(1000);
		this->addChild(hud);
		m_fields->hud = hud;

		if (S::t(O_HITBOX)) applyHitboxSetting();
		if (S::t(O_AUTORETRY)) GameManager::get()->setGameVariable("0026", true);
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		m_fields->timer = 0.f;
	}

	void destroyPlayer(PlayerObject* player, GameObject* object) {
		if (S::t(O_GOD)) return;
		if (S::t(O_NOCLIP) && object != m_anticheatSpike) return; // античит-шип обрабатываем
		if (S::t(O_NOSPIKES) && object && object->m_objectType == GameObjectType::Hazard) return;
		PlayLayer::destroyPlayer(player, object);
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);

		if (S::pendingInstant) {
			S::pendingInstant = false;
			this->levelComplete();
			return;
		}

		m_fields->timer += dt;

		auto p = m_player1;
		if (p) {
			// Reverse Grav: держим гравитацию перевёрнутой
			if (S::t(O_REVGRAV) && !p->m_isUpsideDown) p->flipGravity(true, true);

			// Bunny hop: автопрыжок сразу при приземлении
			if (S::t(O_BUNNY) && !(p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing)) {
				if (p->m_isOnGround) p->pushButton(PlayerButton::Jump);
				else p->releaseButton(PlayerButton::Jump);
			}
		}

		if (S::t(O_HIDEATT) && m_attemptLabel) m_attemptLabel->setVisible(false);

		// HUD: индикатор, FPS, CPS, время, процент
		if (m_fields->hud) {
			// FPS по реальному dt (до спидхака), сглаживание
			if (S::realDt > 0.0001f) S::fps = S::fps * 0.9f + (1.f / S::realDt) * 0.1f;

			double now = S::now();
			while (!S::clicks.empty() && now - S::clicks.front() > 1.0) S::clicks.pop_front();

			std::string s;
			if (S::t(O_CHEATIND)) s += S::anyCheat() ? "[Fatality.win] CHEATING\n" : "[Fatality.win] clean\n";
			if (S::t(O_FPS)) s += fmt::format("FPS: {:.0f}\n", S::fps);
			if (S::t(O_CPS)) s += fmt::format("CPS: {}\n", S::clicks.size());
			if (S::t(O_TIME)) s += fmt::format("Time: {:.2f}\n", m_fields->timer);
			if (S::t(O_SHOWPCT)) s += fmt::format("{:.1f}%\n", this->getCurrentPercent());
			m_fields->hud->setString(s.c_str());
		}
	}
};

class $modify(CheatPlayer, PlayerObject) {
	struct Fields {
		float angle = 0.f;
		bool visualsDirty = false;
		bool glowOn = false;
	};

	bool pushButton(PlayerButton btn) {
		auto pl = PlayLayer::get();
		bool mine = pl && this == pl->m_player1;
		if (mine && btn == PlayerButton::Jump) {
			S::clicks.push_back(S::now());
			// Jump Hack: считаем, что игрок стоит на земле
			if (S::t(O_JUMP)) m_isOnGround = true;
		}
		return PlayerObject::pushButton(btn);
	}

	void playDeathEffect() {
		if (S::t(O_NODEATHFX)) return;
		PlayerObject::playDeathEffect();
	}

	void update(float dt) {
		PlayerObject::update(dt);

		auto pl = PlayLayer::get();
		if (!pl || (this != pl->m_player1 && this != pl->m_player2)) return;

		if (S::t(O_HIDEPLAYER)) this->setVisible(false);
		if (S::t(O_NOWAVE) && m_waveTrail) m_waveTrail->setVisible(false);

		// Glow Cube (розовое свечение)
		if (S::t(O_GLOW) && !m_fields->glowOn) {
			m_fields->glowOn = true;
			m_hasGlow = true;
			this->enableCustomGlowColor({255, 60, 200});
			this->updateGlowColor();
			this->updatePlayerGlow();
		} else if (!S::t(O_GLOW) && m_fields->glowOn) {
			m_fields->glowOn = false;
			this->disableCustomGlowColor();
			this->updateGlowColor();
			this->updatePlayerGlow();
		}

		// Визуальные эффекты работают только на узле иконки (не меняют физику)
		auto node = m_mainLayer;
		if (!node) return;

		bool visuals = S::t(O_SPIN) || S::t(O_AA) || S::cubeSize > 1.01f;
		if (!visuals) {
			if (m_fields->visualsDirty) {
				m_fields->visualsDirty = false;
				node->setRotation(0.f);
				node->setScaleX(1.f);
				node->setScaleY(1.f);
			}
			return;
		}
		m_fields->visualsDirty = true;

		float rot = 0.f;
		if (S::t(O_SPIN)) {
			m_fields->angle = fmodf(m_fields->angle + S::spinSpeed * dt * 60.f, 360.f);
			rot += m_fields->angle;
		}

		float sx = 1.f, sy = 1.f;
		if (S::t(O_AA)) {
			if (S::t(O_FLIPBACK)) sx = -1.f;
			if (S::t(O_FLIPDOWN)) sy = -1.f;
			if (S::t(O_SHAKE)) rot += static_cast<float>(std::rand() % 31) - 15.f;
			if (S::t(O_NLGRAV)) rot += 180.f;
		}
		float k = S::cubeSize > 1.01f ? S::cubeSize : 1.f;

		node->setRotation(rot);
		node->setScaleX(sx * k);
		node->setScaleY(sy * k);
	}
};

// Спидхак + Slow/Fast Motion
class $modify(SpeedScheduler, CCScheduler) {
	void update(float dt) {
		S::realDt = dt;
		CCScheduler::update(dt * S::effectiveSpeed());
	}
};
