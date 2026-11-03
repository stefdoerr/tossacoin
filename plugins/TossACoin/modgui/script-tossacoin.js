/*
 * Toss a Coin — MOD modgui controller.
 *
 * Knobs, the footswitch and the bypass light are bound by MOD-UI from the
 * mod-role attributes. This script only drives the engraved selectors:
 * every <i data-port data-value> writes its value to the LV2 port on click,
 * and the lit word (with its pearl) follows the port on every change -
 * clicks, the settings popup, preset recall, hardware addressing.
 */

function (event, funcs) {

    function show(icon, symbol, value) {
        var v = Math.round(value);
        icon.find('[data-port="' + symbol + '"]').each(function () {
            $(this).toggleClass('on', parseInt($(this).attr('data-value'), 10) === v);
        });
    }

    if (event.type === 'start') {
        var icon = event.icon;
        icon.find('[data-port]').on('click', function (e) {
            e.stopPropagation();
            var symbol = $(this).attr('data-port');
            var value  = parseInt($(this).attr('data-value'), 10);
            funcs.set_port_value(symbol, value);
            show(icon, symbol, value);
        });
        (event.ports || []).forEach(function (p) {
            if (p.symbol === 'steps' || p.symbol === 'sound' || p.symbol === 'reverse')
                show(icon, p.symbol, p.value);
        });
        return;
    }

    if (event.type === 'change') {
        if (event.symbol === 'steps' || event.symbol === 'sound' || event.symbol === 'reverse')
            show(event.icon, event.symbol, event.value);
    }
}
