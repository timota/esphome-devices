"""Stairs effects component exposing the FCOB helper."""

from esphome.const import CONF_ID, CONF_NAME
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import globals as globals_component
from esphome.components import binary_sensor, light, number, select, switch, text_sensor
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect

CODEOWNERS = ["@timota"]

stairs_effects_ns = cg.esphome_ns.namespace("stairs_effects")

StairsEffectsComponent = stairs_effects_ns.class_("StairsEffectsComponent", cg.Component)
StairsFillUpEffect = stairs_effects_ns.class_("StairsFillUpEffect", AddressableLightEffect)
StairsFillDownEffect = stairs_effects_ns.class_("StairsFillDownEffect", AddressableLightEffect)
StairsOffUpEffect = stairs_effects_ns.class_("StairsOffUpEffect", AddressableLightEffect)
StairsOffDownEffect = stairs_effects_ns.class_("StairsOffDownEffect", AddressableLightEffect)

CONF_LED_MAP_ID = "led_map_id"
CONF_PER_LED_ID = "per_led_number_id"
CONF_FADE_STEPS_ID = "fade_steps_number_id"
CONF_ROW_THRESHOLD_ID = "row_threshold_number_id"
CONF_SNAKE_SWITCH_ID = "snake_switch_id"
CONF_WOBBLE_SWITCH_ID = "wobble_switch_id"
CONF_WOBBLE_STRENGTH_ID = "wobble_strength_number_id"
CONF_WOBBLE_FREQ_ID = "wobble_frequency_number_id"
CONF_EASING_SELECT_ID = "easing_select_id"
CONF_SHUTDOWN_DELAY = "shutdown_delay"
CONF_COMPONENT_ID = "component_id"
CONF_LED_COUNT = "led_count"
CONF_MAP_VALID_BINARY_SENSOR = "map_valid_binary_sensor"
CONF_MAP_STATUS_TEXT_SENSOR = "map_status_text_sensor"

COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StairsEffectsComponent),
        cv.Required(CONF_LED_MAP_ID): cv.use_id(globals_component.GlobalsComponent),
        cv.Optional(CONF_LED_COUNT, default=0): cv.int_,
        cv.Optional(CONF_MAP_VALID_BINARY_SENSOR): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_MAP_STATUS_TEXT_SENSOR): text_sensor.text_sensor_schema(),
    }
).extend({})

CONFIG_SCHEMA = cv.ensure_list(COMPONENT_SCHEMA)

async def to_code(config):
    for conf in config:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)

        led_map_var = await cg.get_variable(conf[CONF_LED_MAP_ID])
        cg.add(var.set_led_map(led_map_var))

        cg.add(var.set_led_count(conf[CONF_LED_COUNT]))

        if conf.get(CONF_MAP_VALID_BINARY_SENSOR):
            sens = await binary_sensor.new_binary_sensor(conf[CONF_MAP_VALID_BINARY_SENSOR])
            cg.add(var.set_map_valid_sensor(sens))

        if conf.get(CONF_MAP_STATUS_TEXT_SENSOR):
            txt = await text_sensor.new_text_sensor(conf[CONF_MAP_STATUS_TEXT_SENSOR])
            cg.add(var.set_map_status_sensor(txt))
BASE_EFFECT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_COMPONENT_ID): cv.use_id(StairsEffectsComponent),
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
)


async def _configure_effect(effect_var, config):
    per_led = await cg.get_variable(config[CONF_PER_LED_ID])
    fade_steps = await cg.get_variable(config[CONF_FADE_STEPS_ID])
    row_thr = await cg.get_variable(config[CONF_ROW_THRESHOLD_ID])
    snake_sw = await cg.get_variable(config[CONF_SNAKE_SWITCH_ID])
    wobble_sw = await cg.get_variable(config[CONF_WOBBLE_SWITCH_ID])
    wobble_strength = await cg.get_variable(config[CONF_WOBBLE_STRENGTH_ID])
    wobble_freq = await cg.get_variable(config[CONF_WOBBLE_FREQ_ID])
    easing_sel = await cg.get_variable(config[CONF_EASING_SELECT_ID])

    cg.add(effect_var.set_per_led_number(per_led))
    cg.add(effect_var.set_fade_steps_number(fade_steps))
    cg.add(effect_var.set_row_threshold_number(row_thr))
    cg.add(effect_var.set_snake_switch(snake_sw))
    cg.add(effect_var.set_wobble_switch(wobble_sw))
    cg.add(effect_var.set_wobble_strength_number(wobble_strength))
    cg.add(effect_var.set_wobble_frequency_number(wobble_freq))
    cg.add(effect_var.set_easing_select(easing_sel))
    cg.add(effect_var.set_shutdown_delay(config[CONF_SHUTDOWN_DELAY].total_milliseconds))


@register_addressable_effect(
    "stairs_effects.fill_up", StairsFillUpEffect, "Stairs Fill Up", BASE_EFFECT_SCHEMA
)
async def stairs_effects_fill_up_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_COMPONENT_ID])
    effect = cg.new_Pvariable(effect_id, parent, config[CONF_NAME])
    await _configure_effect(effect, config)
    return effect


@register_addressable_effect(
    "stairs_effects.fill_down", StairsFillDownEffect, "Stairs Fill Down", BASE_EFFECT_SCHEMA
)
async def stairs_effects_fill_down_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_COMPONENT_ID])
    effect = cg.new_Pvariable(effect_id, parent, config[CONF_NAME])
    await _configure_effect(effect, config)
    return effect


@register_addressable_effect(
    "stairs_effects.off_up", StairsOffUpEffect, "Stairs Off Up", BASE_EFFECT_SCHEMA
)
async def stairs_effects_off_up_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_COMPONENT_ID])
    effect = cg.new_Pvariable(effect_id, parent, config[CONF_NAME])
    await _configure_effect(effect, config)
    return effect


@register_addressable_effect(
    "stairs_effects.off_down", StairsOffDownEffect, "Stairs Off Down", BASE_EFFECT_SCHEMA
)
async def stairs_effects_off_down_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_COMPONENT_ID])
    effect = cg.new_Pvariable(effect_id, parent, config[CONF_NAME])
    await _configure_effect(effect, config)
    return effect
