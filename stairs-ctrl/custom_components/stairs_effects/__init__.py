"""Stairs effects component exposing the FCOB helper."""

from esphome.const import CONF_ID
import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@timota"]

stairs_effects_ns = cg.esphome_ns.namespace("stairs_effects")
StairsEffectsComponent = stairs_effects_ns.class_("StairsEffectsComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(StairsEffectsComponent)}).extend({})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
