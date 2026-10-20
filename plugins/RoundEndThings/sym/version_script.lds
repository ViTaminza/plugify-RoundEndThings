{
    global:
        Plugify_*;

        /* s2sdk binding slots (defined by s2sdk_bindings, filled by the plugify module) */
        __s2sdk_*;

        /* permissions binding slots (defined by permissions_bindings, filled by the plugify module
           from the permissions plugin's exported methods): the cookie store of the two
           per-player round-end settings. */
        __permissions_*;

        /* hudmenu binding slots (defined by hudmenu_bindings); pisexcompat references them. */
        __hudmenu_*;

        /* No exported methods: RoundEndThings exposes nothing to other plugins. */

    local: *;
};
