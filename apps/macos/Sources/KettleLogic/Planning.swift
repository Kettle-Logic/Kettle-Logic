import Foundation

// The planning data a recipe keeps: its yeast, water, mash, and brew sessions. It is stored
// with the recipe as JSON, so it survives a reopen. Each tab reads and writes only its own part.

/// Everything the planning tabs keep for one recipe.
struct RecipePlanning: Codable {
    var starter = StarterSettings()
    var water = WaterSettings()
    var mash = MashSettings()
    var sessions: [BrewSession] = []
    /// The brewer's own notes for the recipe. A recipe built from a style starts with its tips.
    var notes = ""

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        starter = try c.decodeIfPresent(StarterSettings.self, forKey: .starter) ?? StarterSettings()
        water = try c.decodeIfPresent(WaterSettings.self, forKey: .water) ?? WaterSettings()
        mash = try c.decodeIfPresent(MashSettings.self, forKey: .mash) ?? MashSettings()
        sessions = try c.decodeIfPresent([BrewSession].self, forKey: .sessions) ?? []
        notes = try c.decodeIfPresent(String.self, forKey: .notes) ?? ""
    }

    /// The planning stored in a recipe's text. Text that does not read back gives the defaults.
    static func read(_ text: String) -> RecipePlanning {
        guard !text.isEmpty, let data = text.data(using: .utf8) else { return RecipePlanning() }
        return (try? JSONDecoder().decode(RecipePlanning.self, from: data)) ?? RecipePlanning()
    }

    var text: String? {
        guard let data = try? JSONEncoder().encode(self) else { return nil }
        return String(data: data, encoding: .utf8)
    }
}

/// Saves a change to the planning of one recipe. The change is applied to the planning as it is
/// stored now, so a tab cannot overwrite what another tab saved.
@MainActor
enum PlanningStore {
    static func update(
        _ recipe: RecipeSnapshot, document: RecipeDocument, store: DocumentStore,
        _ change: (inout RecipePlanning) -> Void
    ) {
        var planning = RecipePlanning.read(recipe.planning)
        change(&planning)
        guard let text = planning.text else { return }
        store.attempt { try document.setPlanning(recipe.id, to: text) }
    }
}

// MARK: Water and mash settings

/// One mineral reading, in mg/L.
struct IonsInput: Codable, Equatable {
    var calcium = 0.0
    var magnesium = 0.0
    var sodium = 0.0
    var sulfate = 0.0
    var chloride = 0.0
    var bicarbonate = 0.0
}

/// A water salt the user adds, and whether it goes in the sparge water.
struct SaltInput: Codable, Equatable, Identifiable {
    var id = UUID()
    var kind = "gypsum"
    var grams = 1.0
    var sparge = false
}

/// An acid the user adds to the mash, in grams of the acid as sold.
struct AcidInput: Codable, Equatable, Identifiable {
    var id = UUID()
    var kind = "lactic acid"
    var grams = 1.0
}

struct WaterSettings: Codable, Equatable {
    var baseName = "Distilled or reverse osmosis"
    var base = IonsInput()
    var salts: [SaltInput] = []
    var leaveOutSalts = false
    var holdSpargeSaltsUntilBoil = false
    /// The share of reverse osmosis water mixed into the base water, in percent.
    var roPercent = 0.0
    /// Whether the desired water is compared with the mash water, and the desired water itself.
    var useTarget = false
    var target = IonsInput(calcium: 140, magnesium: 18, sodium: 25, sulfate: 300, chloride: 55, bicarbonate: 110)

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let defaults = WaterSettings()
        baseName = try c.decodeIfPresent(String.self, forKey: .baseName) ?? defaults.baseName
        base = try c.decodeIfPresent(IonsInput.self, forKey: .base) ?? defaults.base
        salts = try c.decodeIfPresent([SaltInput].self, forKey: .salts) ?? defaults.salts
        leaveOutSalts = try c.decodeIfPresent(Bool.self, forKey: .leaveOutSalts) ?? defaults.leaveOutSalts
        holdSpargeSaltsUntilBoil = try c.decodeIfPresent(Bool.self, forKey: .holdSpargeSaltsUntilBoil)
            ?? defaults.holdSpargeSaltsUntilBoil
        roPercent = try c.decodeIfPresent(Double.self, forKey: .roPercent) ?? defaults.roPercent
        useTarget = try c.decodeIfPresent(Bool.self, forKey: .useTarget) ?? defaults.useTarget
        target = try c.decodeIfPresent(IonsInput.self, forKey: .target) ?? defaults.target
    }
}

/// The mash and sparge the user sets up. The volumes and temperatures are the planning inputs.
struct MashSettings: Codable, Equatable {
    var steps: [MashStepSetting] = [MashStepSetting(name: "Saccharification", kind: MashStepSetting.kinds[0], tempC: 67, minutes: 60)]
    var spargeType = "Batch"
    var spargeTempC = 76.0
    var grainTempC = 20.0
    var tunTempC = 20.0
    var decoctionTempC = 100.0
    var mashRatioLPerKg = 3.0
    var absorptionLPerKg = 1.0
    var deadspaceLiters = 0.5
    var adjustForDeadspace = true
    var acids: [AcidInput] = []
    var acidForTarget = "lactic acid"
    /// The mash pH the acid works toward
    var targetMashPh = 5.4
    var spargeAcid = "phosphoric acid"
    var spargeAcidStrength = 85.0
    var spargeStartPh = 7.0
    var spargeTargetPh = 5.6

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let defaults = MashSettings()
        steps = try c.decodeIfPresent([MashStepSetting].self, forKey: .steps) ?? defaults.steps
        spargeType = try c.decodeIfPresent(String.self, forKey: .spargeType) ?? defaults.spargeType
        spargeTempC = try c.decodeIfPresent(Double.self, forKey: .spargeTempC) ?? defaults.spargeTempC
        grainTempC = try c.decodeIfPresent(Double.self, forKey: .grainTempC) ?? defaults.grainTempC
        tunTempC = try c.decodeIfPresent(Double.self, forKey: .tunTempC) ?? defaults.tunTempC
        decoctionTempC = try c.decodeIfPresent(Double.self, forKey: .decoctionTempC) ?? defaults.decoctionTempC
        mashRatioLPerKg = try c.decodeIfPresent(Double.self, forKey: .mashRatioLPerKg) ?? defaults.mashRatioLPerKg
        absorptionLPerKg = try c.decodeIfPresent(Double.self, forKey: .absorptionLPerKg) ?? defaults.absorptionLPerKg
        deadspaceLiters = try c.decodeIfPresent(Double.self, forKey: .deadspaceLiters) ?? defaults.deadspaceLiters
        adjustForDeadspace = try c.decodeIfPresent(Bool.self, forKey: .adjustForDeadspace) ?? defaults.adjustForDeadspace
        acids = try c.decodeIfPresent([AcidInput].self, forKey: .acids) ?? defaults.acids
        acidForTarget = try c.decodeIfPresent(String.self, forKey: .acidForTarget) ?? defaults.acidForTarget
        targetMashPh = try c.decodeIfPresent(Double.self, forKey: .targetMashPh) ?? defaults.targetMashPh
        spargeAcid = try c.decodeIfPresent(String.self, forKey: .spargeAcid) ?? defaults.spargeAcid
        spargeAcidStrength = try c.decodeIfPresent(Double.self, forKey: .spargeAcidStrength) ?? defaults.spargeAcidStrength
        spargeStartPh = try c.decodeIfPresent(Double.self, forKey: .spargeStartPh) ?? defaults.spargeStartPh
        spargeTargetPh = try c.decodeIfPresent(Double.self, forKey: .spargeTargetPh) ?? defaults.spargeTargetPh
    }
}

/// One step of the mash: its kind, temperature, time, and any addition made during it.
struct MashStepSetting: Codable, Equatable, Identifiable {
    var id = UUID()
    var name: String
    var kind: String
    var tempC: Double
    var minutes: Double
    var addition = ""

    /// Infusion, Temperature, and Decoction, from the engine. The first is the default.
    static let kinds = builtinOptions.mashStepKinds
}

// MARK: Brew sessions

/// One brew session of a recipe: the values measured on the day. Every measured value is optional,
/// so a session is filled in as the brew goes.
struct BrewSession: Codable, Equatable, Identifiable {
    var id = UUID()
    var name = "Session 1"
    var brewDate = Date()

    var mashPh: Double?
    var spargeRunoffPh: Double?
    var endOfRunningsGravity: Double?

    var postMashGravity: Double?
    var preBoilLiters: Double?
    var preBoilGravity: Double?
    var batchLiters: Double?
    var originalGravity: Double?
    var finalGravity: Double?
    var bottlingLiters: Double?

    var carbonationMethod = BrewSession.carbonationMethods[0]
    var carbonationVolumes: Double?
    /// The temperature the beer is held at while force carbonating, in °C.
    var servingTempC: Double? = 4

    var profileName: String?
    var profileSteps: [ProfileStep] = []
    var readings: [FermentationReading] = []

    /// How a batch can be carbonated, from the engine. The first is the default.
    static let carbonationMethods = builtinOptions.carbonationMethods
}

/// A step of a fermentation temperature profile: how many days, at what temperature.
struct ProfileStep: Codable, Equatable, Identifiable {
    var id = UUID()
    var days: Double
    var tempC: Double
}

/// A fermentation temperature profile the user can pick, with its steps in order. The engine supplies them.
struct FermentationPreset: Identifiable, Decodable {
    let name: String
    let steps: [ProfileStep]
    var id: String { name }

    private struct Step: Decodable {
        let days: Double
        let tempC: Double
    }

    private enum CodingKeys: String, CodingKey { case name, steps }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        name = try container.decode(String.self, forKey: .name)
        steps = try container.decode([Step].self, forKey: .steps).map { ProfileStep(days: $0.days, tempC: $0.tempC) }
    }

    static let all: [FermentationPreset] = BuiltinData.load("fermentation_presets")
}

/// One fermentation reading: when it was taken, and the temperature and gravity.
struct FermentationReading: Codable, Equatable, Identifiable {
    var id = UUID()
    var date = Date()
    var temperatureC: Double?
    var gravity: Double?
}

// MARK: Engine requests and results

/// The water and mash inputs, as the engine's brewing plan reads them.
struct BrewingInputs: Codable {
    var baseWater: IonsInput
    var mashRatioLPerKg: Double
    var absorptionLPerKg: Double
    var grainTempC: Double
    var tunTempC: Double
    var mashTempC: Double
    var spargeTempC: Double
    var deadspaceLiters: Double
    var adjustForDeadspace: Bool
    var leaveOutSalts: Bool
    var holdSpargeSaltsUntilBoil: Bool
    var salts: [SaltRequest]
    var acids: [AcidRequest]
    var acidForTarget: String
    var targetMashPh: Double
    var spargeAcid: String
    var spargeAcidStrengthPercent: Double
    var spargeStartPh: Double
    var spargeTargetPh: Double
    var roPercent: Double
    var roWater = IonsInput()
    var targetWater: IonsInput?

    struct SaltRequest: Codable {
        let kind: String
        let grams: Double
        let sparge: Bool
    }

    struct AcidRequest: Codable {
        let kind: String
        let grams: Double
    }

    /// The inputs for a recipe, from its water and mash settings.
    static func from(_ water: WaterSettings, _ mash: MashSettings) -> BrewingInputs {
        BrewingInputs(
            baseWater: water.base,
            mashRatioLPerKg: mash.mashRatioLPerKg,
            absorptionLPerKg: mash.absorptionLPerKg,
            grainTempC: mash.grainTempC,
            tunTempC: mash.tunTempC,
            mashTempC: mash.steps.first?.tempC ?? 67,
            spargeTempC: mash.spargeTempC,
            deadspaceLiters: mash.deadspaceLiters,
            adjustForDeadspace: mash.adjustForDeadspace,
            leaveOutSalts: water.leaveOutSalts,
            holdSpargeSaltsUntilBoil: water.holdSpargeSaltsUntilBoil,
            salts: water.salts.map { SaltRequest(kind: $0.kind, grams: $0.grams, sparge: $0.sparge) },
            acids: mash.acids.map { AcidRequest(kind: $0.kind, grams: $0.grams) },
            acidForTarget: mash.acidForTarget,
            targetMashPh: mash.targetMashPh,
            spargeAcid: mash.spargeAcid,
            spargeAcidStrengthPercent: mash.spargeAcidStrength,
            spargeStartPh: mash.spargeStartPh,
            spargeTargetPh: mash.spargeTargetPh,
            roPercent: water.roPercent,
            targetWater: water.useTarget ? water.target : nil)
    }
}

/// The engine's brewing plan: the volumes, the water profiles, and the acid figures.
struct BrewingPlan: Decodable {
    struct Ions: Decodable {
        let calcium: Double
        let magnesium: Double
        let sodium: Double
        let sulfate: Double
        let chloride: Double
        let bicarbonate: Double
    }

    struct Analysis: Decodable {
        let ions: Ions
        let alkalinity: Double
        let effectiveHardness: Double
        let residualAlkalinity: Double
        let sulfateToChloride: Double?
        let balance: String
    }

    let grainKg: Double
    let strikeLiters: Double
    let absorptionLiters: Double
    let firstRunningsLiters: Double
    let spargeLiters: Double?
    let mashVolumeLiters: Double
    let mashTunVolumeLiters: Double
    let strikeTempC: Double?
    let mashWater: Analysis
    let spargeWater: Analysis
    let acidMilliequivalents: Double
    let alkalinityAfterAcid: Double
    let acidGramsForTarget: Double?
    let estimatedMashPhBeforeAcid: Double
    let estimatedMashPh: Double
    let colorLowSrm: Double?
    let colorHighSrm: Double?
    let maltAcidityMilliequivalents: Double
    let quartsPerPound: Double
    let spargeAcidGrams: Double?
    let spargeAcidMilliliters: Double?
    /// The desired mash water less the mash water, when a target is given.
    let mashDifference: Ions?
    let notes: [String]
}

/// The engine's brew-journal figures: what the recipe expects and what the session measured.
struct JournalPlan: Decodable {
    let expectedBrewhouseEfficiency: Double?
    let expectedPreBoilGravity: Double?
    let expectedOriginalGravity: Double?
    let expectedFinalGravity: Double?
    let expectedAbv: Double?
    let expectedAttenuation: Double?
    let expectedCaloriesPer12Oz: Double?
    let measuredMashEfficiency: Double?
    let measuredBrewhouseEfficiency: Double?
    let measuredAbv: Double?
    let measuredAttenuation: Double?
    let measuredCaloriesPer12Oz: Double?
    let primingSugarGramsPerLiter: Double?
    let primingSugarGrams: Double?
    let forceCarbonationPsi: Double?
}

/// The measured values a session sends to the engine's journal calculations.
struct JournalRequest: Codable {
    var postMashGravity: Double?
    var preBoilLiters: Double?
    var preBoilGravity: Double?
    var batchLiters: Double?
    var originalGravity: Double?
    var finalGravity: Double?
    var bottlingLiters: Double?
    var carbonationVolumes: Double?
    var carbonationTempC: Double?

    init(_ session: BrewSession) {
        carbonationTempC = session.servingTempC
        postMashGravity = session.postMashGravity
        preBoilLiters = session.preBoilLiters
        preBoilGravity = session.preBoilGravity
        batchLiters = session.batchLiters
        originalGravity = session.originalGravity
        finalGravity = session.finalGravity
        bottlingLiters = session.bottlingLiters
        carbonationVolumes = session.carbonationVolumes
    }
}
