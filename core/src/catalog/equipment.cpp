#include "kettlelogic/catalog/equipment.hpp"

namespace kettlelogic::catalog {

const std::vector<EquipmentProfile>& builtInEquipment() {
    static const std::vector<EquipmentProfile> items = {
        EquipmentProfile{
            .name = "Igloo cooler and stovetop kettle",
            .batchLiters = "19", .efficiencyPercent = "70", .evaporationPercent = "12", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "separate tun",
            .note = "Typical starting figures, not searched. Batch size, efficiency, and boil-off are unverified."},
        EquipmentProfile{
            .name = "Anvil Foundry 6.5 gallon",
            .batchLiters = "9.5", .efficiencyPercent = "75", .evaporationPercent = "9", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: Anvil\u2019s ideal 2.5-gallon batch, with up to 8 lb of grain. Efficiency is the vendor\u2019s 75\u201378% claim. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Anvil Foundry 10.5 gallon",
            .batchLiters = "19", .efficiencyPercent = "75", .evaporationPercent = "8", .trubLossLiters = "1.5",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: Anvil\u2019s ideal 5-gallon batch, with up to 16 lb of grain. Efficiency is user-reported 75\u201378%. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Spike Brewing Nano",
            .batchLiters = "57", .efficiencyPercent = "75", .evaporationPercent = "10", .trubLossLiters = "0.5",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "A 1-barrel brewhouse with 55-gallon vessels. This batch is the half-barrel (about 15 gallons). A full barrel is about 117 L. Efficiency and boil-off are not verified."},
        EquipmentProfile{
            .name = "BrewZilla 35 L",
            .batchLiters = "25", .efficiencyPercent = "75", .evaporationPercent = "8", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: vendors give about 25\u201330 L, and this uses the conservative end. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "BrewZilla 65 L",
            .batchLiters = "50", .efficiencyPercent = "75", .evaporationPercent = "7", .trubLossLiters = "1.5",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: vendors give about 50\u201360 L, and this uses the conservative end. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Brewtools B40 Pro",
            .batchLiters = "25", .efficiencyPercent = "75", .evaporationPercent = "8", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: the maximum boil is 40 L and the grain limit is 9 kg, so this is conservative. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Brewtools B80 Pro",
            .batchLiters = "50", .efficiencyPercent = "75", .evaporationPercent = "7", .trubLossLiters = "1.5",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: the maximum boil is 80 L and the grain limit is 20 kg, so this is conservative. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Speidel Braumeister 20 L",
            .batchLiters = "20", .efficiencyPercent = "80", .evaporationPercent = "10", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: 20 L of finished beer, from about 23 L of wort, with a 25 L maximum fill. Efficiency is about 80% from Speidel\u2019s recipe guide. Evaporation and trub are not verified."},
        EquipmentProfile{
            .name = "Grainfather G30",
            .batchLiters = "23", .efficiencyPercent = "75", .evaporationPercent = "8", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: Grainfather lists 25 L as the maximum and 10 L as the minimum, and retailers quote about 23 L. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Grainfather G40",
            .batchLiters = "40", .efficiencyPercent = "75", .evaporationPercent = "8", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: Grainfather lists 40 L after the boil as the maximum, and 10 L as the minimum. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Blichmann BrewEasy 10 gallon",
            .batchLiters = "19", .efficiencyPercent = "75", .evaporationPercent = "9", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: a 5-gallon finished batch, with a 7-gallon maximum with sparging. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "Blichmann BrewEasy 20 gallon",
            .batchLiters = "38", .efficiencyPercent = "75", .evaporationPercent = "8", .trubLossLiters = "1.5",
            .boilMinutes = 60, .mashSystem = "all in one",
            .note = "Batch: a 10-gallon batch, which Blichmann\u2019s manual pairs with a 20-gallon kettle. Evaporation and trub figures are not verified."},
        EquipmentProfile{
            .name = "DigiBoil 9.25 gallon",
            .batchLiters = "19", .efficiencyPercent = "72", .evaporationPercent = "8", .trubLossLiters = "1",
            .boilMinutes = 60, .mashSystem = "separate tun",
            .note = "A boil kettle only, so mash in a separate tun. The maker rates it for 5-gallon extract batches. Evaporation and trub figures are not verified."}
    };
    return items;
}

}  // namespace kettlelogic::catalog
