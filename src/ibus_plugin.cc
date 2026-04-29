#include <ibus.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include "predictable_state_machine.h"

using predictable_pinyin::Phase;
using predictable_pinyin::PredictableStateMachine;
using predictable_pinyin::RimeSession;
using predictable_pinyin::StateSnapshot;

// ---------------------------------------------------------------------------
// Engine struct
// ---------------------------------------------------------------------------

typedef struct _IBusPPEngine IBusPPEngine;
typedef struct _IBusPPEngineClass IBusPPEngineClass;

struct _IBusPPEngine {
  IBusEngine parent;
  RimeSession* session;
  PredictableStateMachine* machine;
  StateSnapshot snapshot;
  IBusLookupTable* table;
  gboolean chinese_mode;
  gboolean shift_pressed;
  IBusPropList* prop_list;
  IBusProperty* mode_prop;
};

struct _IBusPPEngineClass {
  IBusEngineClass parent;
};

#define IBUS_TYPE_PP_ENGINE (ibus_pp_engine_get_type())

G_DEFINE_TYPE(IBusPPEngine, ibus_pp_engine, IBUS_TYPE_ENGINE)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string GetEnvOrDefault(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string(fallback);
}

static std::string DefaultUserDataDir() {
  return GetEnvOrDefault("HOME", "") + "/.config/ibus/rime";
}

static void EnsureRimeUserData(const std::string& user_data_dir,
                               const std::string& shared_data_dir) {
  namespace fs = std::filesystem;
  fs::create_directories(user_data_dir);
  fs::path yaml = fs::path(user_data_dir) / "default.custom.yaml";
  bool need_deploy = !fs::exists(fs::path(user_data_dir) / "build");
  if (!fs::exists(yaml)) {
    std::ofstream(yaml) << "patch:\n  schema_list:\n"
        "    - schema: predictable_pinyin\n"
        "    - schema: luna_pinyin\n"
        "    - schema: pinyin_simp\n";
    need_deploy = true;
  } else {
    std::ifstream in(yaml);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    if (content.find("predictive_pinyin") != std::string::npos) {
      std::string::size_type pos;
      while ((pos = content.find("predictive_pinyin")) != std::string::npos)
        content.replace(pos, 17, "predictable_pinyin");
      std::ofstream(yaml) << content;
      fs::remove_all(fs::path(user_data_dir) / "build");
      need_deploy = true;
    }
  }
  if (need_deploy) {
    std::string cmd = "rime_deployer --build '" + user_data_dir +
                      "' '" + shared_data_dir + "' 2>/dev/null";
    std::system(cmd.c_str());
  }
}

static bool InitializeMachine(IBusPPEngine* engine,
                              const std::string& schema_id) {
  const std::string shared_data_dir =
      GetEnvOrDefault("PREDICTABLE_PINYIN_SHARED_DATA_DIR",
                      "/usr/share/rime-data");
  const std::string user_data_dir =
      GetEnvOrDefault("PREDICTABLE_PINYIN_USER_DATA_DIR",
                      DefaultUserDataDir().c_str());
  EnsureRimeUserData(user_data_dir, shared_data_dir);
  const std::filesystem::path prism_path =
      GetEnvOrDefault("PREDICTABLE_PINYIN_PRISM_PATH",
                      "/usr/share/predictable-pinyin/pinyin_simp.prism.txt");
  const std::filesystem::path stroke_dict_path(GetEnvOrDefault(
      "PREDICTABLE_PINYIN_STROKE_DICT_PATH",
      "/usr/share/predictable-pinyin/stroke.dict.yaml"));
  const std::filesystem::path hanzi_db_path(GetEnvOrDefault(
      "PREDICTABLE_PINYIN_HANZI_DB_PATH",
      "/usr/share/predictable-pinyin/hanzi_db.csv"));
  const std::filesystem::path pinyin_dict_path(GetEnvOrDefault(
      "PREDICTABLE_PINYIN_PINYIN_DICT_PATH",
      "/usr/share/predictable-pinyin/pinyin_simp.dict.yaml"));

  g_message("predictable-pinyin: creating RimeSession shared=%s user=%s schema=%s",
            shared_data_dir.c_str(), user_data_dir.c_str(), schema_id.c_str());
  auto session =
      std::make_unique<RimeSession>(shared_data_dir, user_data_dir, schema_id);
  g_message("predictable-pinyin: RimeSession created");
  auto machine = std::make_unique<PredictableStateMachine>(session.get());
  g_message("predictable-pinyin: initializing machine prism=%s stroke=%s hanzi=%s pinyin_dict=%s",
            prism_path.c_str(), stroke_dict_path.c_str(), hanzi_db_path.c_str(),
            pinyin_dict_path.c_str());
  if (!machine->Initialize(prism_path, stroke_dict_path, hanzi_db_path, pinyin_dict_path)) {
    g_message("predictable-pinyin: machine->Initialize returned false");
    return false;
  }
  g_message("predictable-pinyin: machine initialized, calling Snapshot");
  engine->snapshot = machine->Snapshot();
  engine->machine = machine.release();
  engine->session = session.release();
  return true;
}

static char TranslateKey(guint keyval) {
  if (keyval == IBUS_KEY_space) return ' ';
  if (keyval == IBUS_KEY_BackSpace) return '\b';
  if (keyval == IBUS_KEY_Tab) return '\t';
  if (keyval == IBUS_KEY_semicolon) return ';';
  if (keyval == IBUS_KEY_comma) return ',';
  if (keyval == IBUS_KEY_period) return '.';
  if (keyval == IBUS_KEY_exclam) return '!';
  if (keyval == IBUS_KEY_question) return '?';
  if (keyval == IBUS_KEY_colon) return ':';
  if (keyval == IBUS_KEY_backslash) return '\\';
  if (keyval == IBUS_KEY_parenleft) return '(';
  if (keyval == IBUS_KEY_parenright) return ')';
  if (keyval == IBUS_KEY_bracketleft) return '[';
  if (keyval == IBUS_KEY_bracketright) return ']';
  if (keyval == IBUS_KEY_less) return '<';
  if (keyval == IBUS_KEY_greater) return '>';
  if (keyval == IBUS_KEY_asciitilde) return '~';
  if (keyval == IBUS_KEY_apostrophe) return '\'';
  if (keyval >= IBUS_KEY_a && keyval <= IBUS_KEY_z)
    return static_cast<char>('a' + (keyval - IBUS_KEY_a));
  if (keyval >= IBUS_KEY_A && keyval <= IBUS_KEY_Z)
    return static_cast<char>('a' + (keyval - IBUS_KEY_A));
  return '\0';
}

static std::string BuildPreedit(const StateSnapshot& snap) {
  if (snap.phase == Phase::kIdle) return {};
  std::string preedit =
      snap.preedit.empty() ? snap.pinyin_buffer : snap.preedit;
  if (snap.phase == Phase::kStrokeInput || snap.phase == Phase::kSelecting) {
    preedit += " | ";
    preedit += snap.stroke_buffer;
  }
  return preedit;
}

// ---------------------------------------------------------------------------
// UI update
// ---------------------------------------------------------------------------

static void ibus_pp_engine_update_ui(IBusPPEngine* pp) {
  IBusEngine* engine = IBUS_ENGINE(pp);
  const StateSnapshot& snap = pp->snapshot;

  const std::string preedit_str = BuildPreedit(snap);
  if (preedit_str.empty()) {
    ibus_engine_hide_preedit_text(engine);
    ibus_engine_hide_auxiliary_text(engine);
    ibus_engine_hide_lookup_table(engine);
    return;
  }

  IBusText* preedit = ibus_text_new_from_string(preedit_str.c_str());
  ibus_engine_update_preedit_text(engine, preedit,
                                  ibus_text_get_length(preedit), TRUE);

  if (snap.hint.empty()) {
    ibus_engine_hide_auxiliary_text(engine);
  } else {
    IBusText* auxiliary = ibus_text_new_from_string(snap.hint.c_str());
    ibus_engine_update_auxiliary_text(engine, auxiliary, TRUE);
  }

  if (snap.candidates.empty()) {
    ibus_engine_hide_lookup_table(engine);
    return;
  }

  constexpr int kPageSize = 10;
  ibus_lookup_table_clear(pp->table);
  ibus_lookup_table_set_page_size(pp->table, kPageSize);

  for (std::size_t i = 0; i < snap.candidates.size(); ++i) {
    IBusText* cand = ibus_text_new_from_string(snap.candidates[i].c_str());
    ibus_lookup_table_append_candidate(pp->table, cand);
    if (i < snap.candidate_labels.size()) {
      IBusText* label =
          ibus_text_new_from_string(snap.candidate_labels[i].c_str());
      ibus_lookup_table_set_label(pp->table, i, label);
    }
  }

  ibus_lookup_table_set_cursor_pos(
      pp->table, static_cast<guint>(snap.selected_index));
  ibus_engine_update_lookup_table(engine, pp->table, TRUE);
}

// ---------------------------------------------------------------------------
// IBusEngine virtual method overrides
// ---------------------------------------------------------------------------

static void update_mode_indicator(IBusPPEngine* pp) {
  if (!pp->mode_prop) return;
  ibus_property_set_label(pp->mode_prop,
      ibus_text_new_from_string(pp->chinese_mode ? "CN" : "EN"));
  ibus_property_set_symbol(pp->mode_prop,
      ibus_text_new_from_string(pp->chinese_mode ? "\xe4\xb8\xad" : "EN"));
  ibus_engine_update_property(IBUS_ENGINE(pp), pp->mode_prop);
}

static gboolean ibus_pp_engine_process_key_event(IBusEngine* engine,
                                                  guint keyval,
                                                  guint /*keycode*/,
                                                  guint modifiers) {
  IBusPPEngine* pp = reinterpret_cast<IBusPPEngine*>(engine);

  const guint other_mods = modifiers &
      (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SUPER_MASK | IBUS_MOD4_MASK);

  if (keyval == IBUS_KEY_Shift_L || keyval == IBUS_KEY_Shift_R) {
    if (modifiers & IBUS_RELEASE_MASK) {
      if (pp->shift_pressed) {
        pp->shift_pressed = FALSE;
        pp->chinese_mode = !pp->chinese_mode;
        if (pp->machine) {
          pp->snapshot = pp->machine->Reset();
          ibus_pp_engine_update_ui(pp);
        }
        update_mode_indicator(pp);
        return TRUE;
      }
    } else if (other_mods == 0) {
      pp->shift_pressed = TRUE;
    }
    return FALSE;
  }

  if (!(modifiers & IBUS_RELEASE_MASK))
    pp->shift_pressed = FALSE;

  if (modifiers & IBUS_RELEASE_MASK) return FALSE;
  if (other_mods) return FALSE;

  if (!pp->chinese_mode) return FALSE;

  if (pp->machine == nullptr) return FALSE;

  const char key = TranslateKey(keyval);
  if (key == '\0') {
    return pp->snapshot.phase != Phase::kIdle;
  }

  const Phase prev_phase = pp->snapshot.phase;
  pp->snapshot = pp->machine->HandleKey(key);

  if (!pp->snapshot.commit_text.empty()) {
    IBusText* text =
        ibus_text_new_from_string(pp->snapshot.commit_text.c_str());
    ibus_engine_commit_text(engine, text);
  }

  if (prev_phase == Phase::kIdle && pp->snapshot.phase == Phase::kIdle &&
      pp->snapshot.commit_text.empty()) {
    return FALSE;
  }

  ibus_pp_engine_update_ui(pp);
  return TRUE;
}

static void ibus_pp_engine_reset(IBusEngine* engine) {
  IBusPPEngine* pp = reinterpret_cast<IBusPPEngine*>(engine);
  if (pp->machine != nullptr) {
    pp->snapshot = pp->machine->Reset();
  }
  ibus_engine_hide_preedit_text(engine);
  ibus_engine_hide_auxiliary_text(engine);
  ibus_engine_hide_lookup_table(engine);
  IBUS_ENGINE_CLASS(ibus_pp_engine_parent_class)->reset(engine);
}

static void ibus_pp_engine_focus_in(IBusEngine* engine) {
  IBusPPEngine* pp = reinterpret_cast<IBusPPEngine*>(engine);
  if (pp->prop_list)
    ibus_engine_register_properties(engine, pp->prop_list);
  update_mode_indicator(pp);
  IBUS_ENGINE_CLASS(ibus_pp_engine_parent_class)->focus_in(engine);
}

static void ibus_pp_engine_focus_out(IBusEngine* engine) {
  ibus_pp_engine_reset(engine);
  IBUS_ENGINE_CLASS(ibus_pp_engine_parent_class)->focus_out(engine);
}

static void ibus_pp_engine_enable(IBusEngine* engine) {
  IBUS_ENGINE_CLASS(ibus_pp_engine_parent_class)->enable(engine);
}

static void ibus_pp_engine_disable(IBusEngine* engine) {
  IBUS_ENGINE_CLASS(ibus_pp_engine_parent_class)->disable(engine);
}

// ---------------------------------------------------------------------------
// GObject lifecycle
// ---------------------------------------------------------------------------

static void ibus_pp_engine_init(IBusPPEngine* pp) {
  g_message("predictable-pinyin: engine_init start");
  pp->session = nullptr;
  pp->machine = nullptr;
  new (&pp->snapshot) StateSnapshot();
  pp->table = ibus_lookup_table_new(10, 0, TRUE, TRUE);
  g_object_ref_sink(pp->table);

  pp->chinese_mode = TRUE;
  pp->shift_pressed = FALSE;
  pp->prop_list = ibus_prop_list_new();
  g_object_ref_sink(pp->prop_list);
  pp->mode_prop = ibus_property_new(
      "InputMode", PROP_TYPE_NORMAL,
      ibus_text_new_from_string("CN"), NULL,
      ibus_text_new_from_string("Input Mode"),
      TRUE, TRUE, PROP_STATE_UNCHECKED, NULL);
  ibus_prop_list_append(pp->prop_list, pp->mode_prop);

  try {
    const char* id = std::getenv("PREDICTABLE_PINYIN_SCHEMA_ID");
    if (id && id[0]) {
      g_message("predictable-pinyin: trying schema from env: %s", id);
      InitializeMachine(pp, id);
    } else {
      try {
        g_message("predictable-pinyin: trying predictable_pinyin schema");
        if (!InitializeMachine(pp, "predictable_pinyin"))
          throw std::runtime_error("schema failed");
        g_message("predictable-pinyin: predictable_pinyin schema OK");
      } catch (...) {
        g_message("predictable-pinyin: falling back to pinyin_simp");
        InitializeMachine(pp, "pinyin_simp");
      }
    }
  } catch (...) {
    g_warning("predictable-pinyin: failed to initialize engine");
  }
  g_message("predictable-pinyin: engine_init done, machine=%p", (void*)pp->machine);
}

static void ibus_pp_engine_destroy(IBusPPEngine* pp) {
  delete pp->machine;
  pp->machine = nullptr;
  delete pp->session;
  pp->session = nullptr;
  pp->snapshot.~StateSnapshot();
  if (pp->table) {
    g_object_unref(pp->table);
    pp->table = nullptr;
  }
  pp->mode_prop = nullptr;
  if (pp->prop_list) {
    g_object_unref(pp->prop_list);
    pp->prop_list = nullptr;
  }
  ((IBusObjectClass*)ibus_pp_engine_parent_class)
      ->destroy((IBusObject*)pp);
}

static void ibus_pp_engine_class_init(IBusPPEngineClass* klass) {
  IBusObjectClass* ibus_object_class = IBUS_OBJECT_CLASS(klass);
  IBusEngineClass* engine_class = IBUS_ENGINE_CLASS(klass);

  ibus_object_class->destroy =
      (IBusObjectDestroyFunc)ibus_pp_engine_destroy;

  engine_class->process_key_event = ibus_pp_engine_process_key_event;
  engine_class->reset = ibus_pp_engine_reset;
  engine_class->enable = ibus_pp_engine_enable;
  engine_class->disable = ibus_pp_engine_disable;
  engine_class->focus_in = ibus_pp_engine_focus_in;
  engine_class->focus_out = ibus_pp_engine_focus_out;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

static IBusBus* bus = nullptr;
static IBusFactory* factory = nullptr;

static void on_bus_disconnected(IBusBus* /*bus*/, gpointer /*user_data*/) {
  ibus_quit();
}

int main(int /*argc*/, char** /*argv*/) {
  ibus_init();
  bus = ibus_bus_new();
  g_object_ref_sink(bus);

  if (!ibus_bus_is_connected(bus)) {
    g_warning("predictable-pinyin: not connected to ibus");
    return 1;
  }

  g_signal_connect(bus, "disconnected", G_CALLBACK(on_bus_disconnected),
                   nullptr);

  factory = ibus_factory_new(ibus_bus_get_connection(bus));
  g_object_ref_sink(factory);

  ibus_factory_add_engine(factory, "predictable-pinyin",
                          IBUS_TYPE_PP_ENGINE);

  if (!ibus_bus_request_name(bus, "im.predictablepinyin.PredictablePinyin",
                             0)) {
    g_warning("predictable-pinyin: error requesting bus name");
    return 1;
  }

  ibus_main();

  g_object_unref(factory);
  g_object_unref(bus);
  return 0;
}
