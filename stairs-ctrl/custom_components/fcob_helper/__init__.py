"""FCOB helper component exposing the tracker utilities."""

from esphome.const import CONF_ID
import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@timota"]

fcob_helper_ns = cg.esphome_ns.namespace("fcob_helper")
FcobHelperComponent = fcob_helper_ns.class_("FcobHelperComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(FcobHelperComponent)}).extend({})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
