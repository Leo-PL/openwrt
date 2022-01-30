// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED driver for NU801
 *
 * Kevin Paul Herbert
 * Copyright (c) 2012, Meraki, Inc.
 * Copyright (c) 2022, Lech Perczak
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>

#define NUM_CHANNELS 3

struct led_nu801_led_data {
	struct led_classdev cdev;
	struct led_nu801_data *controller;
	enum led_brightness level;
	bool registered;
	unsigned int index;
};

struct led_nu801_channel {
	unsigned int index;
	enum led_brightness init_brightness;
	struct fwnode_handle *fwnode;
};

struct led_nu801_chip {
	unsigned int index;
	struct led_nu801_channel channels[NUM_CHANNELS];
};

struct led_nu801_data {
	/* Common per chain */
	struct gpio_desc *cki;
	struct gpio_desc *sdi;
	struct gpio_desc *lei;
	struct delayed_work work;
	struct led_nu801_led_data *led_chain;
	int num_chips;
	int num_leds;
	u32 clock_delay_ns;
	u16 *chain_data;
	struct mutex chain_data_mutex;

	struct led_nu801_chip  *chips;
	const char *name;
	atomic_t pending;
};

static void led_nu801_work(struct work_struct *work)
{
	struct led_nu801_data	*controller =
		container_of(work, struct led_nu801_data, work.work);
	u16 bit;
	u16 *chain_data = controller->chain_data;
	u16 brightness;
	unsigned int index;
	unsigned int num_leds = controller->num_leds;
	unsigned int clock_delay_ns = controller->clock_delay_ns;
	static const unsigned int LATCH_DELAY_US = 600;
	struct gpio_desc *cki = controller->cki;
	struct gpio_desc *sdi = controller->sdi;
	struct gpio_desc *lei = controller->lei;

	mutex_lock_io(&controller->chain_data_mutex);
	for (index = 0; index < num_leds; ++index) {
		brightness = chain_data[index];
		for (bit = 0x8000; bit; bit = bit >> 1) {
			gpiod_set_value_cansleep(sdi, !!(brightness & bit));
			gpiod_set_value_cansleep(cki, 1);

			if (unlikely(index == num_leds - 1 && bit == 1 && !lei))
				udelay(LATCH_DELAY_US);
			else
				ndelay(clock_delay_ns);

			gpiod_set_value_cansleep(cki, 0);
			ndelay(clock_delay_ns);
		}
	}
	mutex_unlock(&controller->chain_data_mutex);

	if (lei) {
		gpiod_set_value_cansleep(lei, 1);
		ndelay(clock_delay_ns);
		gpiod_set_value_cansleep(lei, 0);
	}
	atomic_set(&controller->pending, 1);
}

static inline u16 led_nu801_get_pwm_value(enum led_brightness value)
{
	return value << 8 | value;
}

static int led_nu801_set(struct led_classdev *led_cdev,
			  enum led_brightness value)
{
	struct led_nu801_led_data *led_dat =
		container_of(led_cdev, struct led_nu801_led_data, cdev);
	struct led_nu801_data *controller = led_dat->controller;

	if (led_dat->level != value) {
		mutex_lock(&controller->chain_data_mutex);
		controller->chain_data[led_dat->index] = led_nu801_get_pwm_value(value);
		mutex_unlock(&controller->chain_data_mutex);
		led_dat->level = value;
		if (atomic_dec_and_test(&controller->pending))
			schedule_delayed_work(&led_dat->controller->work, (HZ/1000) + 1);
	}
	return 0;
}

static int led_nu801_create(struct led_nu801_data *controller,
				    struct device *parent,
				    int index,
				    struct led_nu801_channel *channel)
{
	struct led_nu801_led_data *led = &controller->led_chain[index];
	struct led_init_data init_data = {};
	int ret;

	if (led->registered) {
		dev_err(parent, "LED channel at index %d already registered due to duplicate node %pOFf in device tree!\n",
				index, to_of_node(channel->fwnode));
		return -EEXIST;
	}

	led->cdev.brightness_set_blocking = led_nu801_set;
	led->level = channel->init_brightness;
	led->index = index;
	controller->chain_data[led->index] = led_nu801_get_pwm_value(channel->init_brightness);
	led->controller = controller;

	init_data.fwnode = channel->fwnode;
	ret = devm_led_classdev_register_ext(parent, &led->cdev, &init_data);
	if (ret < 0)
		return ret;

	led->registered = true;
	return 0;
}

static int
led_nu801_create_chain(struct led_nu801_data *controller,
			struct device *dev)
{
	int ret;
	int chip, chan, num_chips;

	atomic_set(&controller->pending, 1);

	controller->led_chain = devm_kcalloc(dev, controller->num_leds, sizeof(*controller->led_chain), GFP_KERNEL);
	if (!controller->led_chain)
		return -ENOMEM;

	controller->chain_data = devm_kcalloc(dev, controller->num_leds, sizeof(*controller->chain_data), GFP_KERNEL);
	if (!controller->chain_data)
		return -ENOMEM;

	mutex_init(&controller->chain_data_mutex);

	INIT_DELAYED_WORK(&controller->work, led_nu801_work);

	num_chips = controller->num_chips;
	for (chip = 0; chip < num_chips; ++chip) {
		struct led_nu801_chip *led_chip = &controller->chips[chip];
		/* Brightness data is stored backwards in the array,
		 * so logical LED lindexing starts from beginning of chain
		 */
		int chip_index = NUM_CHANNELS * (num_chips - led_chip->index - 1);

		for (chan = 0; chan < NUM_CHANNELS; ++chan) {
			/* The same is true for channels */
			int index = chip_index + NUM_CHANNELS - led_chip->channels[chan].index - 1;

			ret = led_nu801_create(controller, dev, index, &led_chip->channels[chan]);
			if (ret < 0) {
				mutex_destroy(&controller->chain_data_mutex);
				return ret;
			}
		}
	}

	schedule_delayed_work(&controller->work, 0);

	return 0;
}

static void led_nu801_delete(struct device *dev, struct led_nu801_led_data *led)
{
	if (led->registered) {
		if (!(led->cdev.flags & LED_RETAIN_AT_SHUTDOWN))
			led_nu801_set(&led->cdev, LED_OFF);

		devm_led_classdev_unregister(dev, &led->cdev);
	}
}

static void led_nu801_delete_chain(struct device *dev, struct led_nu801_data *controller)
{
	struct led_nu801_led_data *led_chain;
	int num_leds;
	int index;

	led_chain = controller->led_chain;
	num_leds = controller->num_leds;

	for (index = 0; index < num_leds; index++)
		led_nu801_delete(dev, &led_chain[index]);

	flush_delayed_work(&controller->work);
	cancel_delayed_work_sync(&controller->work);

	mutex_destroy(&controller->chain_data_mutex);
}

static int leds_nu801_parse_channel(struct device *dev,
		struct led_nu801_chip *led_chip,
		struct device_node *channel)
{
	int ret;
	int chan_index;
	const char *default_state = "off";
	struct led_nu801_channel *led_channel;

	ret = of_property_read_u32(channel, "reg", &chan_index);
	if (ret) {
		dev_warn(dev, "Missing \"reg\" property in channel node %pOFf\n", &channel);
		return ret;
	}
	if (chan_index >= NUM_CHANNELS) {
		dev_warn(dev, "\"reg\" property in channel node %pOFf over the channel count\n", &channel);
		return -EINVAL;
	}

	led_channel = &led_chip->channels[chan_index];
	led_channel->index = chan_index;
	led_channel->fwnode = of_fwnode_handle(channel);

	led_channel->init_brightness = LED_OFF;
	ret = of_property_read_string(channel, "default-state", &default_state);
	if (ret && ret != -ENOENT)
		dev_warn(dev, "Error getting default-state property %pe", ERR_PTR(ret));

	if (!strncmp(default_state, "on", 4))
		led_channel->init_brightness = LED_FULL;
	else if (strncmp(default_state, "off", 4))
		dev_warn(dev, "Unsupported default-state value %s\n", default_state);
	return 0;
}

static int leds_nu801_parse_chip(struct device *dev,
		struct led_nu801_data *controller,
		struct device_node *chip)
{
	struct device_node *channel;
	int ret;
	int chip_index;
	int chan_index = 0;
	int num_channels;
	struct led_nu801_chip *led_chip;

	ret = of_property_read_u32(chip, "reg", &chip_index);
	if (ret) {
		dev_warn(dev, "Missing \"reg\" property in chip node %pOFf\n", &chip);
		return ret;
	}
	if (chip_index >= controller->num_chips) {
		dev_warn(dev, "\"reg\" property in chip node %pOFf over the chip count\n", &chip);
		return -EINVAL;
	}

	led_chip = &controller->chips[chip_index];
	led_chip->index = chip_index;

	num_channels = of_get_available_child_count(chip);
	if (num_channels == 0) {
		dev_err(dev, "No channels defined\n");
		return -EINVAL;
	} else if (num_channels > 3) {
		dev_err(dev, "Too many channels (%u) defined\n", num_channels);
		return -EINVAL;
	}

	for_each_child_of_node(chip, channel) {
		ret = leds_nu801_parse_channel(dev, led_chip, channel);
		if (ret)
			return ret;
		++chan_index;
	}

	return 0;
}

static struct led_nu801_data *
leds_nu801_create_of(struct device *dev)
{
	struct device_node *np = dev_of_node(dev);
	struct led_nu801_data *controller;
	int ret;
	struct device_node *chip;

	controller = devm_kzalloc(dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return ERR_PTR(-ENOMEM);

	controller->cki = devm_gpiod_get_from_of_node(dev, np, "cki-gpios", 0, GPIOD_OUT_LOW, "nu801-cki");
	if (IS_ERR(controller->cki)) {
		dev_err(dev, "Failed to get CKI GPIO line: %pe\n", controller->cki);
		return ERR_CAST(controller->cki);
	}

	controller->sdi = devm_gpiod_get_from_of_node(dev, np, "sdi-gpios", 0, GPIOD_OUT_LOW, "nu801-sdi");
	if (IS_ERR(controller->sdi)) {
		dev_err(dev, "Failed to get CKI GPIO line: %pe\n", controller->sdi);
		return ERR_CAST(controller->sdi);
	}

	controller->lei = devm_gpiod_get_from_of_node(dev, np, "lei-gpios", 0, GPIOD_OUT_LOW, "nu801-lei");
	if (IS_ERR(controller->lei)) {
		if (PTR_ERR(controller->lei) == -ENOENT) {
			controller->lei = NULL;
		} else {
			dev_err(dev, "Failed to get LEI GPIO line: %pe\n", controller->lei);
			return ERR_CAST(controller->lei);
		}
	}

	controller->clock_delay_ns = 500;
	ret = of_property_read_u32(np, "clock-delay-ns", &controller->clock_delay_ns);
	if (ret)
		dev_warn(dev, "Error getting clock-delay-ns property, defaulting to 500ns: %pe\n", ERR_PTR(ret));

	controller->name = np->name;

	controller->num_chips = of_get_available_child_count(np);
	if (!controller->num_chips) {
		dev_err(dev, "No chips defined\n");
		return ERR_PTR(-EINVAL);
	}

	controller->chips = devm_kcalloc(dev, controller->num_chips,
		sizeof(*controller->chips), GFP_KERNEL);
	if (!controller->chips)
		return ERR_PTR(-ENOMEM);

	controller->num_leds = NUM_CHANNELS * controller->num_chips;

	for_each_available_child_of_node(np, chip) {
		ret = leds_nu801_parse_chip(dev, controller, chip);
		if (ret)
			return ERR_PTR(ret);
	}

	ret = led_nu801_create_chain(controller, dev);
	if (ret < 0)
		return ERR_PTR(ret);
	return controller;
}

static int led_nu801_probe(struct platform_device *pdev)
{
	struct led_nu801_data *controller;

	controller = leds_nu801_create_of(&pdev->dev);
	if (IS_ERR(controller))
		return PTR_ERR(controller);

	platform_set_drvdata(pdev, controller);

	return 0;
}

static int led_nu801_remove(struct platform_device *pdev)
{
	struct led_nu801_data *controller;

	controller = platform_get_drvdata(pdev);

	led_nu801_delete_chain(&pdev->dev, controller);

	return 0;
}

static const struct of_device_id of_numen_leds_match[] = {
	{ .compatible = "numen,nu801", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pwm_leds_match);

static struct platform_driver led_nu801_driver = {
	.probe		= led_nu801_probe,
	.remove		= led_nu801_remove,
	.driver		= {
		.name	= "leds-nu801",
		.owner	= THIS_MODULE,
		.of_match_table = of_numen_leds_match,
	},
};

static int __init led_nu801_init(void)
{
	return platform_driver_register(&led_nu801_driver);
}

static void __exit led_nu801_exit(void)
{
	platform_driver_unregister(&led_nu801_driver);
}

module_init(led_nu801_init);
module_exit(led_nu801_exit);

MODULE_AUTHOR("Kevin Paul Herbert <kph@meraki.net>");
MODULE_DESCRIPTION("NU801 LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-nu801");
