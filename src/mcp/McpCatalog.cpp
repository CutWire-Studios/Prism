#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"

namespace prism::mcp {
namespace {

const QStringList kSourceKinds = {
    QStringLiteral("video"),     QStringLiteral("image"),  QStringLiteral("audio"),
    QStringLiteral("slideshow"), QStringLiteral("camera"), QStringLiteral("canvas"),
    QStringLiteral("shader"),    QStringLiteral("html"),   QStringLiteral("text"),
    QStringLiteral("ndi")};

const QStringList kPanicModes = {QStringLiteral("none"), QStringLiteral("blackout"),
                                 QStringLiteral("freeze"), QStringLiteral("stay_tuned")};

const QStringList kDecks = {QStringLiteral("a"), QStringLiteral("b")};

QJsonObject clipRefProps()
{
    return {{QStringLiteral("clip"),
             stringProp(QStringLiteral(
                 "Clip id from inspect({clips:true}) — the node id as a string or number. "
                 "Required; ops never fall back to the current deck assignment."))}};
}

QJsonObject mergeProps(QJsonObject a, const QJsonObject &b)
{
    for (auto it = b.begin(); it != b.end(); ++it)
        a.insert(it.key(), it.value());
    return a;
}

struct Op {
    const char *name;
    const char *toolbox;
    const char *when;
    const char *description;
    QJsonObject schema;
    bool readOnly = false;
    bool destructive = false;
    bool idempotent = false;
};

const QList<Op> &ops()
{
    static const QList<Op> k = {
        {"list_clips", "sources", "See what is on the graph",
         "List input clips. Returns {clips:[{id, name, kind, path, activeA, activeB}]}. "
         "inspect({clips:true}) is richer.",
         objectSchema({}), true, false, true},
        {"list_source_kinds", "sources", "See which source types add_source accepts",
         "List kinds that add_source / add_clip understand, with a short hint for each.",
         objectSchema({}), true, false, true},
        {"list_cameras", "sources", "Pick a camera for add_source",
         "Enumerate attached cameras. Returns {cameras:[{index, id, label, default}]}. "
         "Pass id (preferred) or index to add_source({kind:\"camera\"}).",
         objectSchema({}), true, false, true},
        {"list_ndi_sources", "sources", "Pick an NDI sender for add_source",
         "Discover NDI senders on the LAN (blocks up to ~2s). Returns {sources:[name,…]} "
         "or not_found when NDI is not built in.",
         objectSchema({}), true, false, true},
        {"add_clip", "sources", "Bring a media file onto the graph",
         "Add a local video, image, or audio file as an input clip. Auto-detects kind from "
         "the path. Returns {id, name, kind}. Does not assign it to a deck — call select_a "
         "or select_b with the returned id.",
         objectSchema({{QStringLiteral("path"),
                        stringProp(QStringLiteral("Absolute file path"))},
                       {QStringLiteral("name"),
                        stringProp(QStringLiteral("Display name (default: file stem)"))}},
                      {QStringLiteral("path")})},
        {"add_source", "sources", "Create a live or generated source",
         "Add a non-file source. kind is required. Extra fields depend on kind: "
         "text needs text; html needs html; shader needs code (a default gradient is used if "
         "omitted); camera uses camera/index; slideshow needs path (folder); canvas uses "
         "w/h/fill/color; ndi needs ndi (sender name from list_ndi_sources). Returns {id, name, kind}.",
         objectSchema({{QStringLiteral("kind"),
                        enumProp(QStringLiteral("Source kind"), kSourceKinds)},
                       {QStringLiteral("name"), stringProp(QStringLiteral("Display name"))},
                       {QStringLiteral("path"),
                        stringProp(QStringLiteral("File or folder path (video/image/audio/slideshow)"))},
                       {QStringLiteral("text"), stringProp(QStringLiteral("text kind: overlay string"))},
                       {QStringLiteral("html"), stringProp(QStringLiteral("html kind: HTML/CSS/JS"))},
                       {QStringLiteral("code"), stringProp(QStringLiteral("shader kind: GLSL fragment"))},
                       {QStringLiteral("camera"),
                        stringProp(QStringLiteral("camera kind: device id from list_cameras"))},
                       {QStringLiteral("index"),
                        integerProp(QStringLiteral("camera kind: 0-based index if camera id omitted"))},
                       {QStringLiteral("ndi"),
                        stringProp(QStringLiteral("ndi kind: sender name from list_ndi_sources"))},
                       {QStringLiteral("w"), integerProp(QStringLiteral("canvas kind: width px (default 1280)"))},
                       {QStringLiteral("h"), integerProp(QStringLiteral("canvas kind: height px (default 720)"))},
                       {QStringLiteral("fill"),
                        enumProp(QStringLiteral("canvas kind: fill"),
                                 {QStringLiteral("checkered"), QStringLiteral("transparent"),
                                  QStringLiteral("color")})},
                       {QStringLiteral("color"),
                        stringProp(QStringLiteral("canvas/text: #RRGGBB or #AARRGGBB"))}},
                      {QStringLiteral("kind")})},
        {"remove_clip", "sources", "Delete an input clip",
         "Remove a clip from the graph. If it is on a deck, that deck is cleared.",
         objectSchema(clipRefProps(), {QStringLiteral("clip")}), false, true},
        {"rename_clip", "sources", "Change a clip's display name",
         "Rename a clip. Does not rename the file on disk.",
         objectSchema(mergeProps(clipRefProps(),
                                 {{QStringLiteral("name"),
                                   stringProp(QStringLiteral("New display name"))}}),
                      {QStringLiteral("clip"), QStringLiteral("name")})},
        {"set_text", "sources", "Update a text overlay",
         "Replace the string (and optional colour) on a text clip. Fails type_mismatch if the "
         "clip is not kind text. Decks showing it reload.",
         objectSchema(mergeProps(clipRefProps(),
                                 {{QStringLiteral("text"), stringProp(QStringLiteral("New overlay text"))},
                                  {QStringLiteral("color"),
                                   stringProp(QStringLiteral("Fill colour #RRGGBB or #AARRGGBB"))}}),
                      {QStringLiteral("clip"), QStringLiteral("text")})},
        {"set_html", "sources", "Update an HTML overlay",
         "Replace htmlContent on an html clip. Fails type_mismatch otherwise.",
         objectSchema(mergeProps(clipRefProps(),
                                 {{QStringLiteral("html"),
                                   stringProp(QStringLiteral("HTML/CSS/JS document"))}}),
                      {QStringLiteral("clip"), QStringLiteral("html")})},
        {"set_shader", "sources", "Update a GLSL source",
         "Replace shaderCode on a shader clip. Fails type_mismatch otherwise.",
         objectSchema(mergeProps(clipRefProps(),
                                 {{QStringLiteral("code"),
                                   stringProp(QStringLiteral("GLSL fragment shader"))}}),
                      {QStringLiteral("clip"), QStringLiteral("code")})},

        {"select_a", "decks", "Put a clip on deck A (preview / left)",
         "Assign a clip to deck A and start it. Same as clicking A on the clip card.",
         objectSchema(clipRefProps(), {QStringLiteral("clip")})},
        {"select_b", "decks", "Put a clip on deck B (program-side / right)",
         "Assign a clip to deck B and start it. Same as clicking B on the clip card.",
         objectSchema(clipRefProps(), {QStringLiteral("clip")})},
        {"play_a", "decks", "Play deck A", "Start playback on deck A.", objectSchema({})},
        {"pause_a", "decks", "Pause deck A", "Pause playback on deck A.", objectSchema({})},
        {"play_b", "decks", "Play deck B", "Start playback on deck B.", objectSchema({})},
        {"pause_b", "decks", "Pause deck B", "Pause playback on deck B.", objectSchema({})},
        {"seek_a", "decks", "Scrub deck A",
         "Seek deck A to `at` seconds. No-op for live sources.",
         objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Seconds from start"))}},
                      {QStringLiteral("at")})},
        {"seek_b", "decks", "Scrub deck B",
         "Seek deck B to `at` seconds. No-op for live sources.",
         objectSchema({{QStringLiteral("at"), numberProp(QStringLiteral("Seconds from start"))}},
                      {QStringLiteral("at")})},
        {"set_speed", "decks", "Change a deck's playback rate",
         "Set deck speed. Clamped to 0.25–4.0. Only affects seekable file sources.",
         objectSchema({{QStringLiteral("deck"), enumProp(QStringLiteral("Which deck"), kDecks)},
                       {QStringLiteral("speed"),
                        numberProp(QStringLiteral("Playback rate (1 = normal)"))}},
                      {QStringLiteral("deck"), QStringLiteral("speed")})},

        {"set_fader", "transition", "Move the T-bar",
         "Set the crossfader. 0 = full A, 100 = full B. Stops a running AUTO animation.",
         objectSchema({{QStringLiteral("value"),
                        integerProp(QStringLiteral("0..100"))}},
                      {QStringLiteral("value")})},
        {"cut", "transition", "Hard-cut to the other deck",
         "Instant cut: snap the fader to the opposite end (A↔B).", objectSchema({})},
        {"auto_transition", "transition", "Animate the T-bar to the other deck",
         "Run AUTO: ease the fader to the opposite end over the current transition duration.",
         objectSchema({})},
        {"list_transitions", "transition", "See available transition modes",
         "Returns {modes:[name,…], current, index, duration}.", objectSchema({}), true, false, true},
        {"set_transition", "transition", "Pick the transition look",
         "Set the transition by name (from list_transitions) or 0-based index.",
         objectSchema({{QStringLiteral("mode"),
                        stringProp(QStringLiteral("Transition name from list_transitions"))},
                       {QStringLiteral("index"),
                        integerProp(QStringLiteral("0-based index if mode is omitted"))}})},
        {"set_duration", "transition", "How long AUTO takes",
         "Set AUTO duration in seconds.",
         objectSchema({{QStringLiteral("seconds"),
                        numberProp(QStringLiteral("Duration in seconds"))}},
                      {QStringLiteral("seconds")})},

        {"set_panic", "panic", "Emergency program output",
         "none = clear; blackout = black frame; freeze = hold the current frame; "
         "stay_tuned = branded slate. Audio is muted for every mode except none.",
         objectSchema({{QStringLiteral("mode"), enumProp(QStringLiteral("Panic mode"), kPanicModes)}},
                      {QStringLiteral("mode")})},

        {"start_recording", "output", "Record program",
         "Start program recording. Pass dir if no save location is set yet. Returns {dir}.",
         objectSchema({{QStringLiteral("dir"),
                        stringProp(QStringLiteral("Output folder (optional if already set in the UI)"))}})},
        {"stop_recording", "output", "Stop every recorder",
         "Stop program and per-track recordings.", objectSchema({})},
        {"recording_status", "output", "See if we are recording",
         "Returns {active, elapsed, tracks, dir}.", objectSchema({}), true, false, true},
        {"set_ndi", "output", "Send program over NDI",
         "Enable or disable NDI program output. Optional name is the NDI stream name.",
         objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("On/off"))},
                       {QStringLiteral("name"), stringProp(QStringLiteral("NDI stream name"))}},
                      {QStringLiteral("enabled")})},
        {"set_virtual_camera", "output", "Send program to a virtual camera",
         "Enable or disable the virtual camera sink (v4l2loopback / DirectShow).",
         objectSchema({{QStringLiteral("enabled"), boolProp(QStringLiteral("On/off"))}},
                      {QStringLiteral("enabled")})},

        {"save_session", "session", "Write the open session to disk",
         "Save the current graph, decks, and mixer state to a .psm file. Path is required.",
         objectSchema({{QStringLiteral("path"),
                        stringProp(QStringLiteral("Absolute .psm path"))}},
                      {QStringLiteral("path")})},
        {"load_session", "session", "Open a saved session",
         "Load a .psm session, replacing the current graph. Path is required.",
         objectSchema({{QStringLiteral("path"),
                        stringProp(QStringLiteral("Absolute .psm path"))}},
                      {QStringLiteral("path")}),
         false, true},
    };
    return k;
}

QJsonObject opTool(const Op &op)
{
    return toolDef(QString::fromUtf8(op.name),
                   QStringLiteral("When: %1. %2")
                       .arg(QString::fromUtf8(op.when), QString::fromUtf8(op.description)),
                   op.schema, toolAnnotations(op.readOnly, op.destructive, op.idempotent));
}

QJsonArray endpointList()
{
    QJsonArray endpoints;
    endpoints.append(QStringLiteral("/mcp"));
    for (const QString &name : toolboxNames())
        endpoints.append(QStringLiteral("/mcp/") + name);
    return endpoints;
}

} // namespace

QStringList toolboxNames()
{
    return {QStringLiteral("sources"), QStringLiteral("decks"), QStringLiteral("transition"),
            QStringLiteral("panic"),   QStringLiteral("output"), QStringLiteral("session")};
}

QString agentGuideText()
{
    return QStringLiteral(
        "Prism MCP agent guide\n"
        "\n"
        "Workflow:\n"
        "1. Call catalog on POST /mcp (homepage).\n"
        "2. Call toolbox({name}) for JSON schemas of ops in that toolbox.\n"
        "3. Call apply({ops:[{tool, args}, …]}) to run one or many mutations in order.\n"
        "4. Call inspect({clips:true, detail:true}) for clip ids, decks, fader, and recording.\n"
        "5. Call capture() for a JPEG still of program output (use to verify a take).\n"
        "\n"
        "Pinned endpoints (/mcp/sources, /mcp/decks, …) list toolbox ops directly. "
        "catalog, toolbox, and apply are only on /mcp; inspect and capture work on both. "
        "Toolbox ops may also be called by name directly on /mcp instead of through apply.\n"
        "\n"
        "Conventions:\n"
        "- Times are seconds. Fader 0 = deck A, 100 = deck B.\n"
        "- Identify clips by id from inspect({clips:true}). Clip ops never fall back to the\n"
        "  current deck assignment — pass clip every time.\n"
        "- add_clip / add_source only put a node on the graph. Call select_a or select_b to\n"
        "  put it on a deck, then cut / auto_transition / set_fader to take it to program.\n"
        "- Every op returns {ok:true, …} or {ok:false, error:<code>, detail:<text>}. Codes:\n"
        "  bad_args, not_found, type_mismatch, unknown_op, unknown_toolbox, wrong_endpoint,\n"
        "  wrong_toolbox, apply_failed, capture_failed, conflict.\n"
        "- apply is not atomic: on failure the ops before it stay applied. Check stopped/failed.\n"
        "- There is no MCP undo. Live takes are immediate.\n"
        "\n"
        "Live take pattern:\n"
        "1. add_clip or add_source, read id from the reply.\n"
        "2. select_b (or select_a) with that id — the off-air deck.\n"
        "3. capture() to confirm the frame.\n"
        "4. cut or auto_transition to take it to program.\n"
        "\n"
        "Toolboxes: sources, decks, transition, panic, output, session.\n");
}

QJsonObject catalogPayload()
{
    struct Box {
        const char *name;
        const char *when;
    };
    static const Box boxes[] = {
        {"sources", "Add, list, rename, and edit input clips (files, text, HTML, shaders, cameras)."},
        {"decks", "Assign clips to A/B, play/pause/seek, deck speed."},
        {"transition", "T-bar, CUT, AUTO, transition look and duration."},
        {"panic", "Emergency program output: blackout, freeze, stay-tuned slate."},
        {"output", "Record program, NDI, virtual camera."},
        {"session", "Save or load a .psm session file."},
    };

    QJsonArray toolboxes;
    for (const Box &box : boxes) {
        QJsonArray opEntries;
        for (const Op &op : ops()) {
            if (qstrcmp(op.toolbox, box.name) == 0) {
                opEntries.append(QJsonObject{
                    {QStringLiteral("name"), QString::fromUtf8(op.name)},
                    {QStringLiteral("when"), QString::fromUtf8(op.when)},
                });
            }
        }
        toolboxes.append(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(box.name)},
            {QStringLiteral("when"), QString::fromUtf8(box.when)},
            {QStringLiteral("ops"), opEntries},
        });
    }

    return ok({
        {QStringLiteral("toolboxes"), toolboxes},
        {QStringLiteral("endpoints"), endpointList()},
        {QStringLiteral("units"),
         QJsonObject{{QStringLiteral("time"), QStringLiteral("seconds")},
                     {QStringLiteral("fader"), QStringLiteral("0=A, 100=B")},
                     {QStringLiteral("clipId"), QStringLiteral("node id from inspect")}}},
        {QStringLiteral("workflow"),
         QStringLiteral("catalog → toolbox({name}) → apply({ops:[{tool,args}…]})")},
        {QStringLiteral("hint"),
         QStringLiteral("toolbox({name}) then apply({ops:[{tool,args}…]}) for a batch. "
                        "inspect({clips:true,detail:true}) for clip ids and mixer state. "
                        "capture() for a program still.")},
        {QStringLiteral("limitations"),
         QJsonArray{
             QStringLiteral("apply is not atomic — on failure the ops before it stay applied; check stopped/failed."),
             QStringLiteral("apply cannot run catalog, toolbox, inspect, capture, or apply; call those directly on /mcp."),
             QStringLiteral("Clip ops need clip — they never fall back to the current deck assignment."),
             QStringLiteral("add_clip / add_source do not put the clip on a deck; call select_a / select_b."),
             QStringLiteral("There is no MCP undo. Live takes are immediate."),
             QStringLiteral("Graph wiring (process nodes, layers, A/B select) is not editable over MCP yet."),
             QStringLiteral("Screen and window capture still need an interactive OS picker — they are not MCP-addable."),
         }},
        {QStringLiteral("guide"), agentGuideText()},
    });
}

QJsonObject toolboxPayload(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (!toolboxNames().contains(key))
        return err("unknown_toolbox",
                   QStringLiteral("Known: %1").arg(toolboxNames().join(QLatin1Char(' '))));

    QJsonArray tools;
    for (const Op &op : ops()) {
        if (key == QLatin1String(op.toolbox))
            tools.append(opTool(op));
    }
    return ok({{QStringLiteral("name"), key}, {QStringLiteral("tools"), tools}});
}

QJsonArray homepageTools()
{
    const QStringList toolboxEnum = toolboxNames();
    QJsonArray tools;
    tools.append(toolDef(QStringLiteral("catalog"),
                         QStringLiteral("When: Start here. Returns toolboxes, per-op when hints, "
                                        "endpoints, units, and workflow (no schemas)."),
                         objectSchema({}), toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("toolbox"),
        QStringLiteral("When: Load schemas. Returns full JSON schemas for one toolbox's ops. "
                       "Then call those ops via apply."),
        objectSchema({{QStringLiteral("name"), enumProp(QStringLiteral("Toolbox name"), toolboxEnum)}},
                     {QStringLiteral("name")})));
    tools.append(toolDef(
        QStringLiteral("inspect"),
        QStringLiteral(
            "When: Read mixer state. Returns revision, fader, decks, transition, panic, recording, "
            "NDI, virtual camera, program size. clips=true adds the clip list. detail=true expands "
            "each clip with kind/path/content. since=<revision> returns {unchanged:true, revision} "
            "when nothing changed."),
        objectSchema({{QStringLiteral("clips"), boolProp(QStringLiteral("Include the clip list"))},
                      {QStringLiteral("detail"),
                       boolProp(QStringLiteral("Expand clip rows with kind, path, and content fields"))},
                      {QStringLiteral("since"),
                       integerProp(QStringLiteral("Revision from a prior inspect; returns {unchanged:true} when current"))}}),
        toolAnnotations(true, false, true)));
    tools.append(toolDef(
        QStringLiteral("apply"),
        QStringLiteral(
            "When: Mutate. Runs ops in order and stops at the first failure. NOT atomic — ops "
            "before the failure stay applied; the reply is {ok:false, error:\"apply_failed\", "
            "stopped:<index>, failed:<that op's error>, done:[results so far]}. On success: "
            "{ok:true, n, done}. Only toolbox ops go here — catalog, toolbox, inspect, capture, "
            "and apply itself return unknown_op, so call those directly."),
        objectSchema({{QStringLiteral("ops"),
                       arrayProp(objectSchema({{QStringLiteral("tool"),
                                                stringProp(QStringLiteral("Toolbox op name, e.g. select_a"))},
                                               {QStringLiteral("args"),
                                                QJsonObject{{QStringLiteral("type"),
                                                             QStringLiteral("object")}}}},
                                              {QStringLiteral("tool")}),
                                 QStringLiteral("Sequential operations"))}},
                     {QStringLiteral("ops")})));
    tools.append(toolDef(
        QStringLiteral("capture"),
        QStringLiteral(
            "When: Verify visually. JPEG still of program output, scaled to a 1280px long edge, "
            "plus {w, h, full}. full=true instead writes a full-resolution PNG next to recordings "
            "and returns its path. Cannot be used inside apply."),
        objectSchema({{QStringLiteral("full"),
                       boolProp(QStringLiteral("Full-res PNG on disk instead of inline JPEG"))}}),
        toolAnnotations(true, false, true)));
    return tools;
}

QJsonArray toolboxDirectTools(const QString &name)
{
    const QString key = name.trimmed().toLower();
    QJsonArray tools;
    for (const Op &op : ops()) {
        if (key == QLatin1String(op.toolbox))
            tools.append(opTool(op));
    }
    return tools;
}

bool isHomepageTool(const QString &name)
{
    static const QStringList k = {QStringLiteral("catalog"), QStringLiteral("toolbox"),
                                  QStringLiteral("inspect"), QStringLiteral("apply"),
                                  QStringLiteral("capture")};
    return k.contains(name);
}

bool isKnownOp(const QString &name)
{
    for (const Op &op : ops()) {
        if (name == QLatin1String(op.name))
            return true;
    }
    return false;
}

bool isReadOnlyOp(const QString &name)
{
    for (const Op &op : ops()) {
        if (name == QLatin1String(op.name))
            return op.readOnly;
    }
    return false;
}

QString toolboxForOp(const QString &name)
{
    for (const Op &op : ops()) {
        if (name == QLatin1String(op.name))
            return QString::fromUtf8(op.toolbox);
    }
    return {};
}

QString homepageHtml()
{
    const QJsonObject cat = catalogPayload();
    QString body = QStringLiteral(
                       "<!doctype html><meta charset=utf-8><title>Prism MCP</title>"
                       "<body style='font:14px/1.45 system-ui;max-width:42rem;margin:2rem auto;padding:0 1rem'>"
                       "<h1>Prism agent access</h1>"
                       "<p>This mixer is exposing an MCP server on localhost. Any local process with the "
                       "session token can drive decks, transitions, sources, and capture frames.</p>"
                       "<p><strong>Workflow:</strong> %1</p>"
                       "<p>Agents: POST JSON-RPC to <code>/mcp</code> with "
                       "<code>Authorization: Bearer …</code>.</p>"
                       "<h2>Toolboxes</h2><ul>")
                       .arg(cat.value(QStringLiteral("workflow")).toString());
    const QJsonArray boxes = cat.value(QStringLiteral("toolboxes")).toArray();
    for (const QJsonValue &v : boxes) {
        const QJsonObject b = v.toObject();
        QStringList names;
        for (const QJsonValue &op : b.value(QStringLiteral("ops")).toArray())
            names.append(op.toObject().value(QStringLiteral("name")).toString());
        body += QStringLiteral("<li><strong>%1</strong> — %2<br><code>%3</code></li>")
                    .arg(b.value(QStringLiteral("name")).toString(),
                         b.value(QStringLiteral("when")).toString(),
                         names.join(QStringLiteral(", ")));
    }
    QStringList endpointStrings;
    for (const QJsonValue &ep : cat.value(QStringLiteral("endpoints")).toArray())
        endpointStrings.append(QStringLiteral("<code>%1</code>").arg(ep.toString()));
    body += QStringLiteral("</ul><p>Pinned endpoints: %1.</p></body>")
                .arg(endpointStrings.join(QStringLiteral(", ")));
    return body;
}

} // namespace prism::mcp
