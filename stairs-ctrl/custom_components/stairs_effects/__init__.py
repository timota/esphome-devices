"""Stairs effects component exposing the FCOB helper."""

from esphome.const import CONF_ID
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import globals as globals_component
from esphome.components import number, select, switch

CODEOWNERS = ["@timota"]

stairs_effects_ns = cg.esphome_ns.namespace("stairs_effects")

StairsEffectsComponent = stairs_effects_ns.class_("StairsEffectsComponent", cg.Component)

CONF_MAP_ID = "map_id"
CONF_PER_LED_ID = "per_led_number_id"
CONF_FADE_STEPS_ID = "fade_steps_number_id"
CONF_ROW_THRESHOLD_ID = "row_threshold_number_id"
CONF_SNAKE_SWITCH_ID = "snake_switch_id"
CONF_WOBBLE_SWITCH_ID = "wobble_switch_id"
CONF_WOBBLE_STRENGTH_ID = "wobble_strength_number_id"
CONF_WOBBLE_FREQ_ID = "wobble_frequency_number_id"
CONF_EASING_SELECT_ID = "easing_select_id"
CONF_SHUTDOWN_DELAY = "shutdown_delay"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StairsEffectsComponent),
        cv.Required(CONF_MAP_ID): cv.use_id(globals_component.GlobalsComponent),
        cv.Required(CONF_PER_LED_ID): cv.use_id(number.Number),
        cv.Required(CONF_FADE_STEPS_ID): cv.use_id(number.Number),
        cv.Required(CONF_ROW_THRESHOLD_ID): cv.use_id(number.Number),
        cv.Required(CONF_SNAKE_SWITCH_ID): cv.use_id(switch.Switch),
        cv.Required(CONF_WOBBLE_SWITCH_ID): cv.use_id(switch.Switch),
        cv.Required(CONF_WOBBLE_STRENGTH_ID): cv.use_id(number.Number),
        cv.Required(CONF_WOBBLE_FREQ_ID): cv.use_id(number.Number),
        cv.Required(CONF_EASING_SELECT_ID): cv.use_id(select.Select),
        cv.Optional(CONF_SHUTDOWN_DELAY, default="50ms"): cv.positive_time_period_milliseconds,
    }
).extend({})

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    map_var = await globals_component.get_variable(config[CONF_MAP_ID])
    cg.add(var.set_led_map(map_var))
    per_led = await cg.get_variable(config[CONF_PER_LED_ID])
    fade_steps = await cg.get_variable(config[CONF_FADE_STEPS_ID])
    row_thr = await cg.get_variable(config[CONF_ROW_THRESHOLD_ID])
    snake_sw = await cg.get_variable(config[CONF_SNAKE_SWITCH_ID])
    wobble_sw = await cg.get_variable(config[CONF_WOBBLE_SWITCH_ID])
    wobble_strength = await cg.get_variable(config[CONF_WOBBLE_STRENGTH_ID])
    wobble_freq = await cg.get_variable(config[CONF_WOBBLE_FREQ_ID])
    easing_sel = await cg.get_variable(config[CONF_EASING_SELECT_ID])

    cg.add(var.set_per_led_number(per_led))
    cg.add(var.set_fade_steps_number(fade_steps))
    cg.add(var.set_row_threshold_number(row_thr))
    cg.add(var.set_snake_switch(snake_sw))
    cg.add(var.set_wobble_switch(wobble_sw))
    cg.add(var.set_wobble_strength_number(wobble_strength))
    cg.add(var.set_wobble_frequency_number(wobble_freq))
    cg.add(var.set_easing_select(easing_sel))
    cg.add(var.set_shutdown_delay(config[CONF_SHUTDOWN_DELAY].total_milliseconds))
