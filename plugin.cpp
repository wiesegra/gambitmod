#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
}

// --- GAMBIT STATE MACHINE ---
class Gambit {
public:
    static Gambit* GetSingleton() {
        static Gambit singleton;
        return &singleton;
    }

    int GetComboState() {
        if (combo_chain.empty()) return 0;
        try {
            return std::stoi(combo_chain);
        } catch (...) {
            logger::info("Invalid Combo State: {}", combo_chain);
            return -1;
        }
    }

    void Add_A() {
        if (combo_chain.length() >= 6) {
            logger::info("Max Combo Length Reached");
        } else {
            combo_chain += "1";
            logger::info("Builder: Normal Attack (1) -> Chain: {}", combo_chain);
        }
    }

    void Add_B() {
        if (combo_chain.length() >= 6) {
            logger::info("Max Combo Length Reached");
        } else {
            combo_chain += "2";
            logger::info("Builder: Bash Attack (2) -> Chain: {}", combo_chain);
        }
    }

    void Clear() {
        combo_chain.clear();
        logger::info("Gambit Chain Cleared.");
    }

private:
    std::string combo_chain = "";
};

// --- EXECUTION LOGIC (Moved outside sinks) ---
void execute_gambit(RE::PlayerCharacter* player) {
    auto g = Gambit::GetSingleton();
    int comboID = g->GetComboState();

    // If no combo exists, do nothing and let the normal power attack play
    if (comboID == 0) return;

    logger::info("Attempting Cashout for ID: {}", comboID);

    switch (comboID) {
            // --- TIER 1 ---
        case 11:  // A -> A (Deft Strike) -> Self Heal
            logger::info("Effect: Deft Strike (Self Heal)");
            break;

        case 22:  // B -> B (Defensive Strike) -> Stamina Restore
            logger::info("Effect: Defensive Strike (Stamina Surge)");
            break;

            // --- TIER 2 ---
        case 112:  // A -> A -> B (Shout Finisher)

            // These events play on a loop, player cannot get out of them. Will need to figure out how to make it work with both a shout type and a bash type cashout

            logger::info("Effect: Perseverance");
            player->NotifyAnimationGraph("IdleForceDefaultState");

            player->NotifyAnimationGraph("ShoutStart");

            player->NotifyAnimationGraph("ShoutRelease");

            player->NotifyAnimationGraph("IdleForceDefaultState");

            break;

        case 121:  // A -> B -> A (The Boot)
            logger::info("Effect: The Boot");
            break;

        default:
            logger::info("Unknown Combo.");
            break;
    }

    // Clear the chain after cashout
    g->Clear();
}

// --- 1. HIT SINK (BUILDERS ONLY) ---
class HitEventSink : public RE::BSTEventSink<RE::TESHitEvent> {
private:
    bool EventValidate(const RE::TESHitEvent* event) {
        if (!event || !event->cause || !event->target) return false;
        if (!event->target->As<RE::Actor>()) return false;
        if (!event->cause->IsPlayerRef()) return false;
        return true;
    }

public:
    RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override {
        if (!EventValidate(event)) return RE::BSEventNotifyControl::kContinue;

        auto flags = event->flags;
        bool isBash = flags.any(RE::TESHitEvent::Flag::kBashAttack);
        bool isPower = flags.any(RE::TESHitEvent::Flag::kPowerAttack);


        if (isPower) {
            return RE::BSEventNotifyControl::kContinue; // DO not want power attacks to contribute
        }


        if (isBash) {
            Gambit::GetSingleton()->Add_B();
        } else {
            // Normal Attack Validation
            auto sourceForm = RE::TESForm::LookupByID(event->source);
            if (sourceForm && sourceForm->Is(RE::FormType::Weapon)) {
                auto weapon = sourceForm->As<RE::TESObjectWEAP>();
                if (weapon && weapon->IsMelee()) {
                    Gambit::GetSingleton()->Add_A();
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    static HitEventSink* GetSingleton() {
        static HitEventSink singleton;
        return &singleton;
    }
};

// --- 2. ANIMATION SINK (CASHOUTS ONLY) ---
class AnimEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* event,
                                          RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override {

        // 1. Basic Null Checks
        if (!event || !event->holder) return RE::BSEventNotifyControl::kContinue;

        // 2. CHECK: Is this the player? (Read-Only Check)
        // We look at the event holder without trying to modify it.
        // IsPlayerRef() is a const function, so this is perfectly safe.
        if (!event->holder->IsPlayerRef()) {
            return RE::BSEventNotifyControl::kContinue;
        }
        // 3. FETCH: Get the Mutable Player Singleton
        auto* player = RE::PlayerCharacter::GetSingleton();

        // Safety check just in case the singleton isn't ready (rare in gameplay)
        if (!player) return RE::BSEventNotifyControl::kContinue;
        std::string_view eventTag = event->tag.data();

        bool isPowerAttackStart = (eventTag.find("PowerAttack_Start_End") != std::string_view::npos);

        if (isPowerAttackStart) {
            // Now we pass the safe, mutable singleton to your function
            logger::info("We are now going to do execute_gambit");
            execute_gambit(player);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    static AnimEventSink* GetSingleton() {
        static AnimEventSink singleton;
        return &singleton;
    }
};

// --- REGISTRATION ---
void RegisterAnimSink() {
    auto player = RE::PlayerCharacter::GetSingleton();

    logger::info("registering anim sink");
    if (player) {
        player->AddAnimationGraphEventSink(AnimEventSink::GetSingleton());
        logger::info("Gambit: Animation Sink Attached.");
    } else {
        logger::info("registering anim sink failed");
    }
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    // Combine New Game and Post Load Game into one check
    if (message->type == SKSE::MessagingInterface::kPostLoadGame ||
        message->type == SKSE::MessagingInterface::kNewGame) {
        RegisterAnimSink();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();

    logger::info("Gambit Plugin Loaded.");

    // 1. Hit Event (Builders)
    auto* eventsrc = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventsrc) {
        eventsrc->AddEventSink(HitEventSink::GetSingleton());
    }

    // 2. Messaging (Anim Sink Registration)
    auto* messaging = SKSE::GetMessagingInterface();
    if (messaging) {
        messaging->RegisterListener(OnMessage);
    }

    return true;
}