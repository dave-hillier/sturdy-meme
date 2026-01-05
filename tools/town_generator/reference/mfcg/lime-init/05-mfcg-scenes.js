/*
 * lime-init/05-mfcg-scenes.js
 * Part 5/8: MFCG Scenes and UI
 * Contains: com.watabou.mfcg.scenes.*, overlays, tools, forms
 */
        a.build(Math.floor(20 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 20))
    });
    this.buttons.add(b);
    b = new fb("Large");
    b.set_width(80);
    b.click.add(function() {
        a.build(Math.floor(40 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * 40))
    });
    this.buttons.add(b);
    this.txtSize =
        new tc;
    this.txtSize.set_centered(!0);
    this.txtSize.set_width(80);
    this.txtSize.set_prompt("Size");
    this.txtSize.set_restrict("0-9");
    this.txtSize.enter.add(l(this, this.onCustomSize));
    this.buttons.add(this.txtSize);
    b = new fb("Rebuild");
    b.set_width(80);
    b.click.add(l(this, this.rebuild));
    this.buttons.add(b);
    b = new fb("From URL");
    b.set_width(80);
    b.click.add(l(this, this.fromURL));
    this.buttons.add(b);
    this.bg = ta.light();
    this.add(this.tabs);
    this.add(this.bg);
    this.add(this.buttons)
};
g["com.watabou.mfcg.ui.forms.GenerateForm"] =
    Kd;
Kd.__name__ = "com.watabou.mfcg.ui.forms.GenerateForm";
Kd.__super__ = vc;
Kd.prototype = v(vc.prototype, {
    getTitle: function() {
        return "Generate"
    },
    layout: function() {
        this.buttons.set_x(this.bg.set_x(this.tabs.get_width()));
        this.rWidth = this.tabs.get_width() + this.buttons.get_width();
        this.rHeight = Math.max(this.tabs.get_height(), this.buttons.get_height());
        this.bg.setSize(this.buttons.get_width(), this.rHeight)
    },
    onKey: function(a) {
        if (13 == a) this.build(Ub.nextSize);
        else return !1;
        return !0
    },
    getCheckBox: function(a, b) {
        b =
            new ud(b);
        b.set_value(ba.get(a));
        return b
    },
    createFeaturesTab: function() {
        var a = this,
            b = new gb;
        this.chkRandom = this.getCheckBox("random", "Random");
        this.chkRandom.changed.add(l(this, this.onRandom));
        b.add(this.chkRandom);
        var c = ta.black();
        c.set_height(2);
        c.halign = "fill";
        b.add(c);
        c = new Pd(2);
        c.setMargins(0, 8);
        var d = 0;
        for (this.chkFeatures = new Qa; d < Kd.features.length;) {
            var f = Kd.features[d],
                h = [Kd.features[d + 1]],
                k = this.getCheckBox(h[0], f);
            k.set_enabled(!this.chkRandom.get_value() || -1 != Kd.nonRandom.indexOf(f));
            k.changed.add(function(b) {
                return function(c) {
                    "citadel" != b[0] || c || a.chkFeatures.h.urban_castle.set_value(!1);
                    "urban_castle" == b[0] && c && a.chkFeatures.h.citadel.set_value(!0)
                }
            }(h));
            this.chkFeatures.h[h[0]] = k;
            c.add(k);
            d += 2
        }
        b.add(c);
        c = ta.black();
        c.set_height(2);
        c.halign = "fill";
        return b
    },
    onRandom: function(a) {
        for (var b = this.chkFeatures.h, c = Object.keys(b), d = c.length, f = 0; f < d;) {
            var h = b[c[f++]]; - 1 == Kd.nonRandom.indexOf(h.get_text()) && h.set_enabled(!a)
        }
    },
    createRoadsTab: function() {
        var a = this,
            b = new Pd(2);
        b.setMargins(10,
            8);
        this.chkDefault = new ud;
        b.add(this.chkDefault);
        b.add(new Ib("Default number"));
        this.chkMaximum = this.getCheckBox("hub", null);
        b.add(this.chkMaximum);
        b.add(new Ib("Maximum number"));
        this.chkSpecific = new ud;
        this.chkSpecific.valign = "center";
        b.add(this.chkSpecific);
        var c = ba.get("gates");
        this.txtGates = new tc(-1 < c ? null == c ? "null" : "" + c : "");
        this.txtGates.set_restrict("0-9");
        this.txtGates.set_prompt("Number");
        this.txtGates.set_width(100);
        this.txtGates.update.add(function(b) {
            a.chkDefault.set_value(!1);
            a.chkMaximum.set_value(!1);
            a.chkSpecific.set_value(!0)
        });
        this.txtGates.enter.add(function(b) {
            a.build(Ub.nextSize)
        });
        b.add(this.txtGates);
        c = -1 < c ? 2 : ba.get("hub") ? 1 : 0;
        new hg([this.chkDefault, this.chkMaximum, this.chkSpecific], c);
        return b
    },
    onCustomSize: function(a) {
        a = H.parseInt(a);
        null != a && 5 <= a && 200 >= a && this.build(a)
    },
    saveFeatures: function() {
        ba.set("random", this.chkRandom.get_value());
        for (var a = this.chkFeatures.h, b = Object.keys(a), c = b.length, d = 0; d < c;) {
            var f = b[d++];
            ba.set(f, a[f].get_value())
        }
        ba.set("hub", this.chkMaximum.get_value());
        ba.set("gates", this.chkSpecific.get_value() ? H.parseInt(this.txtGates.get_text()) : -1)
    },
    build: function(a) {
        this.saveFeatures();
        new Ub(Fd.create(a, C.seed));
        bb.switchScene(Ec);
        this.stage.set_focus(this)
    },
    rebuild: function() {
        this.saveFeatures();
        new Ub(Fd.similar(Ub.instance.bp));
        bb.switchScene(Ec)
    },
    update: function() {
        this.chkRandom.set_value(ba.get("random", !0));
        for (var a = this.chkFeatures.h, b = Object.keys(a), c = b.length, d = 0; d < c;) {
            var f = b[d++];
            a[f].set_value(ba.get(f, !0))
        }
        this.onRandom(this.chkRandom.get_value());
        a = ba.get("gates");
        this.chkDefault.set_value(!ba.get("hub") && -1 >= a);
        this.chkMaximum.set_value(ba.get("hub") && -1 >= a);
        this.chkSpecific.set_value(-1 < a) && this.txtGates.set_text(null == a ? "null" : "" + a)
    },
    fromURL: function() {
        null == u.findWidnow(Jf) && u.showDialog(new Jf)
    },
    __class__: Kd
});
var rg = function() {
    var a = this;
    oc.call(this);
    this.setMargins(10, 8);
    this.model = Ub.instance;
    this.txtName = new tc(this.model.name);
    this.txtName.set_centered(!0);
    this.txtName.enter.add(l(this, this.onEnterName));
    this.txtName.leave.add(function() {
        a.txtName.set_text(a.model.name)
    });
    this.txtName.set_width(200);
    this.add(this.txtName);
    this.townInfo = new pi(this.model);
    this.townInfo.halign = "center";
    this.add(this.townInfo);
    this.addSection("Reroll names");
    this.addButtonRow("Town", l(this, this.rerollName), "Districts", (G = this.model, l(G, G.rerollDistricts)));
    this.addSection("Points of interest");
    this.addButtonRow("Load", l(this, this.onLoadPOIs), "Clear", l(this, this.onClearPOIs));
    this.addSeparator();
    this.addButtonRow("Warp", l(this, this.onWarp), "Overworld", l(this, this.onOverworld));
    var b = new ed;
    b.setMargins(0, 8);
    var c = new fb("Permalink");
    c.set_width(96);
    c.click.add(l(this, this.onCopyURL));
    b.add(c);
    c = new fe("Export", ["PNG", "SVG", "JSON"]);
    c.set_width(96);
    c.action.add(l(this, this.onExport));
    b.add(c);
    this.add(b);
    Bb.newModel.add(l(this, this.onNewModel));
    Bb.titleChanged.add(l(this, this.onTitleChanged));
    Bb.geometryChanged.add(l(this, this.onGeometryChanged))
};
g["com.watabou.mfcg.ui.forms.TownForm"] = rg;
rg.__name__ = "com.watabou.mfcg.ui.forms.TownForm";
rg.__super__ = vc;
rg.prototype = v(vc.prototype, {
    onHide: function() {
        vc.prototype.onHide.call(this);
        Bb.newModel.remove(l(this, this.onNewModel));
        Bb.titleChanged.remove(l(this, this.onTitleChanged));
        Bb.geometryChanged.remove(l(this, this.onGeometryChanged))
    },
    getTitle: function() {
        return "Settlement"
    },
    addSeparator: function() {
        var a = ta.black();
        a.set_height(2);
        a.halign = "fill";
        this.add(a)
    },
    addSection: function(a) {
        this.addSeparator();
        this.add(new Ib(a))
    },
    addButtonRow: function(a, b, c, d) {
        var f = new ed;
        f.setMargins(0, 8);
        a = new fb(a);
        a.set_width(96);
        a.click.add(b);
        f.add(a);
        b = new fb(c);
        b.set_width(96);
        b.click.add(d);
        f.add(b);
        this.add(f)
    },
    onEnterName: function(a) {
        this.model.setName(a, !0);
        this.stage.set_focus(this)
    },
    rerollName: function() {
        this.model.setName(this.model.rerollName())
    },
    onLoadPOIs: function() {
        var a = this,
            b = new Gf;
        b.addEventListener("select", function(c) {
            b.addEventListener("complete", l(a, a.onPOIsLoaded));
            b.load()
        });
        b.browse([new Th("JSON list", "*.json")])
    },
    onPOIsLoaded: function(a) {
        a = va.__cast(a.target, Gf).data.toString();
        a = JSON.parse(a);
        this.model.addLandmarks(a)
    },
    onClearPOIs: function() {
        this.model.removeLandmarks()
    },
    onWarp: function() {
        bb.switchScene(jd)
    },
    onOverworld: function() {
        var a = new If("https://azgaar.github.io/Fantasy-Map-Generator/");
        a.method = "GET";
        a.data = this.model.getFMGParams();
        a.data.from = "MFCG";
        Ra.getURL(a, "fmg")
    },
    onCopyURL: function() {
        null == u.findWidnow(Jf) && u.showDialog(new Jf)
    },
    onExport: function(a) {
        switch (a) {
            case "JSON":
                be.asJSON();
                break;
            case "PNG":
                be.asPNG();
                break;
            case "SVG":
                be.asSVG()
        }
    },
    onNewModel: function(a) {
        this.model = a;
        this.txtName.set_text(a.name);
        this.townInfo.update(a)
    },
    onTitleChanged: function(a) {
        this.txtName.set_text(a)
    },
    onGeometryChanged: function(a) {
        this.townInfo.update(this.model)
    },
    __class__: rg
});
var ke = function() {
    oc.call(this);
    this.tabs = new ig;
    this.tabs.set_rowSize(4);
    this.tabs.addTab("Graphics", this.colors());
    this.tabs.addTab("Elements", this.elements());
    this.tabs.addTab("Buildings", this.buildings());
    this.tabs.addTab("Outline", this.outline());
    this.tabs.addTab("Text", this.text());
    this.tabs.addTab("Misc", this.misc());
    this.add(this.tabs);
    this.tabs.change.add(function(a) {
        ke.lastTab = a
    });
    this.tabs.onTab(ke.lastTab)
};
g["com.watabou.mfcg.ui.forms.StyleForm"] = ke;
ke.__name__ = "com.watabou.mfcg.ui.forms.StyleForm";
ke.__super__ = vc;
ke.prototype = v(vc.prototype, {
    getTitle: function() {
        return "Style"
    },
    addCheckbox: function(a, b, c, d, f) {
        a = new ud(a);
        a.set_value(ba.get(b, c));
        a.changed.add(function(a) {
            ba.set(b, a);
            f(a)
        });
        d.add(a);
        return a
    },
    addDropDown: function(a, b, c, d, f) {
        this.addLabel(a, d);
        a = Rc.ofStrings(c);
        a.set_value(ba.get(b));
        a.set_width(120);
        a.set_centered(!0);
        a.update.add(function(a) {
            ba.set(b, a);
            f(a)
        });
        d.add(a);
        return a
    },
    addFont: function(a,
        b, c, d) {
        this.addLabel(a, d);
        a = new fd(ba.get(b, c), Id.font2text, null, fd.editInForm(Id, a, this));
        a.set_width(180);
        a.update.add(function(a) {
            ba.set(b, a);
            bb.switchScene(Ec)
        });
        d.add(a)
    },
    addSeparator: function(a) {
        var b = ta.black();
        b.set_height(2);
        b.halign = "fill";
        a.add(b)
    },
    addLabel: function(a, b) {
        a = new Ib(a);
        a.valign = "center";
        b.add(a)
    },
    colors: function() {
        var a = new gb;
        a.setMargins(10, 8);
        var b = new fb("Color scheme", ia.editColors);
        a.add(b);
        this.addCheckbox("Thin lines", "thin_lines", !1, a, function(a) {
            K.thinLines = a;
            bb.switchScene(Ec)
        });
        this.addCheckbox("Tint districts", "watercolours", !1, a, function(a) {
            ia.inst.drawMap()
        });
        this.addCheckbox("Weathered roofs", "weathered_roofs", !1, a, function(a) {
            "Block" != ba.get("display_mode", "Lots") && ia.inst.drawMap()
        });
        return a
    },
    elements: function() {
        var a = new gb;
        a.setMargins(10, 8);
        var b = new Pd(2);
        b.setMargins(0, 8);
        this.addLabel("Font size", b);
        var c = Rc.ofInts(["Small", "Medium", "Large"]);
        c.set_value(ba.get("text_size", 1));
        c.set_width(120);
        c.set_centered(!0);
        c.update.add(function(a) {
            ba.set("text_size", a);
            bb.switchScene(Ec)
        });
        b.add(c);
        this.addDropDown("Districts", "districts", kd.DISTRICTS_MODE, b, function(a) {
            ia.inst.layoutLabels()
        });
        this.addDropDown("Landmarks", "landmarks", kd.LANDMARK_MODES, b, function(a) {
            ia.inst.toggleOverlays()
        });
        a.add(b);
        this.addSeparator(a);
        b = new Pd(3);
        b.setMargins(0, 10);
        this.addCheckbox("Title", "city_name", !0, b, function(a) {
            ia.inst.toggleOverlays()
        });
        this.addCheckbox("Scale bar", "scale_bar", !0, b, function(a) {
            ia.inst.toggleOverlays()
        });
        this.addCheckbox("Emblem", "emblem", !1, b, function(a) {
            ia.inst.toggleOverlays()
        });
        this.addCheckbox("Grid", "grid", !0, b, function(a) {
            ia.inst.toggleOverlays()
        });
        this.addCheckbox("Compass", "compass", !0, b, function(a) {
            ia.inst.toggleOverlays()
        });
        a.add(b);
        return a
    },
    buildings: function() {
        var a = new gb;
        a.setMargins(10, 8);
        var b = new Pd(2);
        b.setMargins(0, 8);
        this.addDropDown("Display mode", "display_mode", kd.DISPLAY_MODES, b, function(a) {
            ia.inst.drawMap()
        });
        this.addDropDown("Processing", "processing", kd.PROCESSES, b, function(a) {
            Ub.instance.updateLots();
            ia.inst.drawMap()
        });
        this.addDropDown("Roofs", "roof_style",
            kd.ROOF_STYLES, b,
            function(a) {
                ia.inst.drawMap()
            });
        a.add(b);
        this.addCheckbox("Raised", "raised", !0, a, function(a) {
            ia.inst.drawMap()
        });
        this.addCheckbox("Solids", "draw_solids", !0, a, function(a) {
            ia.inst.drawMap()
        });
        return a
    },
    outline: function() {
        var a = new ed;
        a.setMargins(10, 20);
        var b = new gb;
        b.setMargins(0, 8);
        var c = Object.create(null),
            d = this.addCheckbox("Buildings", "outline_buildings", !0, b, function(a) {
                ia.inst.drawMap()
            });
        c.outline_buildings = d;
        d = !1;
        null == d && (d = !0);
        d = this.addCheckbox("Solids", "outline_solids",
            d, b,
            function(a) {
                ia.inst.drawMap()
            });
        c.outline_solids = d;
        d = this.addCheckbox("Water", "outline_water", !0, b, function(a) {
            ia.inst.drawMap()
        });
        c.outline_water = d;
        d = this.addCheckbox("Roads", "outline_roads", !0, b, function(a) {
            ia.inst.drawMap()
        });
        c.outline_roads = d;
        d = this.addCheckbox("Trees", "outline_trees", !0, b, function(a) {
            ia.inst.drawMap()
        });
        c.outline_trees = d;
        d = !1;
        null == d && (d = !0);
        d = this.addCheckbox("Fields", "outline_fields", d, b, function(a) {
            ia.inst.drawMap()
        });
        c.outline_fields = d;
        a.add(b);
        b = new fb("Toggle all",
            function() {
                var a = c,
                    b = a,
                    d = Object.keys(a);
                a = 0;
                b = !b[d[a++]].get_value();
                d = a = c;
                a = Object.keys(a);
                for (var n = a.length, p = 0; p < n;) {
                    var g = a[p++],
                        q = g;
                    d[g].set_value(b);
                    ba.set(q, b)
                }
                ia.inst.drawMap()
            });
        a.add(b);
        return a
    },
    text: function() {
        var a = new Pd(2);
        a.setMargins(10, 8);
        this.addFont("Title", "font_title", vb.fontTitle, a);
        this.addFont("Labels", "font_label", vb.fontLabel, a);
        this.addFont("Legend", "font_legend", vb.fontLegend, a);
        this.addFont("Pins", "font_pin", vb.fontPin, a);
        this.addFont("Elements", "font_element", vb.fontElement,
            a);
        return a
    },
    misc: function() {
        var a = new gb;
        a.setMargins(10, 8);
        var b = null;
        this.addCheckbox("Show alleys", "show_alleys", !1, a, function(a) {
            ia.map.updateRoads()
        });
        this.addCheckbox("Show trees", "show_trees", !1, a, function(a) {
            b.set_enabled(a);
            ia.inst.drawMap()
        });
        b = this.addCheckbox("Show forests", "show_forests", !1, a, function(a) {
            ia.inst.drawMap()
        });
        b.set_enabled(ba.get("show_trees", !1));
        var c = new Pd(2);
        c.setMargins(0, 8);
        this.addDropDown("Towers", "towers", kd.TOWER_SHAPES, c, function(a) {
            ia.inst.drawMap()
        });
        this.addDropDown("Farm fields",
            "farm_fileds", kd.FIELD_MODES, c,
            function(a) {
                ia.inst.drawMap()
            });
        a.add(c);
        return a
    },
    __class__: ke
});
var Ec = function() {
    this.mouse = new I;
    ia.call(this);
    this.btnMenu = new fb("Menu", l(this, this.onMenu));
    sb.preview || u.layer.addChild(this.btnMenu);
    this.fader = Ke.create(1, l(this, this.onFadeOut))
};
g["com.watabou.mfcg.scenes.ViewScene"] = Ec;
Ec.__name__ = "com.watabou.mfcg.scenes.ViewScene";
Ec.__super__ = ia;
Ec.prototype = v(ia.prototype, {
    activate: function() {
        ia.prototype.activate.call(this);
        if (!sb.preview) {
            u.layer.addChild(new le);
            le.inst.awake.add(l(this, this.onAwake));
            this.stage.addEventListener("mouseMove", l(this, this.onMouseMove));
            this.stage.addEventListener("click", l(this, this.onClick));
            vc.loadSaved(Ec.tools);
            var a = Ub.instance.bp;
            if (null != a.export) {
                switch (a.export.toLowerCase()) {
                    case "json":
                        be.asJSON();
                        break;
                    case "png":
                        be.asPNG();
                        break;
                    case "svg":
                        be.asSVG()
                }
                a.export = null
            }
        }
    },
    deactivate: function() {
        ia.prototype.deactivate.call(this);
        u.layer.removeChild(this.btnMenu);
        this.stage.removeEventListener("mouseMove", l(this, this.onMouseMove));
        this.stage.removeEventListener("click", l(this, this.onClick));
        null != le.inst && u.layer.removeChild(le.inst)
    },
    onKeyEvent: function(a, b) {
        if (b) switch (a) {
            case 9:
            case 71:
                this.toggleWindow(Kd);
                break;
            case 13:
                this.buildNew();
                break;
            case 49:
                this.loadPreset("default");
                break;
            case 50:
                this.loadPreset("ink");
                break;
            case 51:
                this.loadPreset("bw");
                break;
            case 52:
                this.loadPreset("vivid");
                break;
            case 53:
                this.loadPreset("natural");
                break;
            case 54:
                this.loadPreset("modern");
                break;
            case 65:
                this.toggleAlleys();
                break;
            case 66:
                this.keyShift ?
                    this.toggleBuildings() : this.showBuildings();
                break;
            case 67:
                ia.editColors();
                break;
            case 68:
                this.toggleGrid();
                break;
            case 69:
                this.showElements();
                break;
            case 76:
                this.toggleDistricts();
                break;
            case 78:
                this.toggleThinLines();
                break;
            case 79:
                this.showOutlines();
                break;
            case 83:
                this.toggleWindow(ke);
                break;
            case 84:
                this.toggleWindow(rg);
                break;
            case 87:
                this.onWarp();
                break;
            default:
                ia.prototype.onKeyEvent.call(this, a, b)
        }
    },
    layout: function() {
        ia.prototype.layout.call(this);
        this.btnMenu.set_x(u.layer.get_width() - this.btnMenu.get_width() -
            2);
        this.btnMenu.set_y(2)
    },
    onFadeOut: function(a) {
        a = 1 - a;
        this.btnMenu.set_alpha(le.inst.set_alpha(a * a * (3 - 2 * a)))
    },
    onAwake: function(a) {
        a ? (this.fader.stop(), this.onFadeOut(0)) : this.fader.start()
    },
    onMouseMove: function(a) {
        Sd.__neq(a.target, this.stage) || (a = ia.map, this.mouse.setTo(a.get_mouseX(), a.get_mouseY()), a = this.patch = this.model.getCell(this.mouse), a = null != a ? a.ward.getLabel() : null, le.inst.set(a))
    },
    onClick: function(a) {
        null != this.patch && (a.commandKey || a.controlKey ? this.patch.reroll() : a.shiftKey && (null !=
            this.model.focus ? this.zoomIn(null) : null != this.patch.district && this.zoomIn(this.patch.district)))
    },
    onMenu: function() {
        this.onContext(ia.getMenu());
        this.showMenu(this.btnMenu)
    },
    onWarp: function() {
        bb.switchScene(jd)
    },
    createOverlays: function() {
        ia.prototype.createOverlays.call(this);
        this.overlays.push(this.title = new qi(this));
        this.addChild(this.title);
        this.overlays.push(this.emblem = new nf(this));
        this.addChild(this.emblem)
    },
    toggleOverlays: function(a) {
        null == a && (a = !1);
        ia.prototype.toggleOverlays.call(this,
            a);
        this.title.set_visible(ba.get("city_name", !0) && !this.legend.get_visible());
        this.emblem.set_visible(ba.get("emblem", !1) && !this.legend.get_visible());
        if (sb.preview) {
            a = 0;
            for (var b = this.overlays; a < b.length;) {
                var c = b[a];
                ++a;
                c.set_visible(!1)
            }
        }
    },
    arrangeOverlays: function() {
        ia.prototype.arrangeOverlays.call(this);
        var a = this.emblem;
        if (this.legend.get_visible()) switch (this.legend.get_position()._hx_index) {
            case 1:
            case 4:
                var b = yc.TOP_RIGHT;
                break;
            default:
                b = yc.TOP_LEFT
        } else b = yc.TOP_LEFT;
        a.set_position(b)
    },
    onMapContext: function(a) {
        var b = this;
        a.addItem("Add landmark", l(this, this.addLandmark));
        null != this.patch && this.patch.isRerollable() && (a.addItem("Reroll geometry", (G = this.patch, l(G, G.reroll))), this.patch.ward.onContext(a, this.mouse.x, this.mouse.y));
        if (null == this.model.focus) {
            var c = this.patch;
            c = null != (null != c ? c.district : null)
        } else c = !1;
        c && a.addItem("Zoom in", function() {
            b.zoomIn(b.patch.district)
        })
    },
    onContext: function(a) {
        var b = this;
        null != this.model.focus && (a.addItem("Zoom out", function() {
                b.zoomIn(null)
            }),
            a.addSeparator());
        a.addItem("New city", l(this, this.buildNew));
        a.addItem("Warp", l(this, this.onWarp));
        a.addItem("Colors...", ia.editColors);
        var c = new dd;
        c.addItem("PNG", be.asPNG);
        c.addItem("SVG", be.asSVG);
        c.addItem("JSON", be.asJSON);
        a.addSubmenu("Export as", c);
        a.addSeparator();
        c = function(c, f) {
            a.addItem(c, function() {
                b.toggleWindow(f)
            }, null != u.findWidnow(f))
        };
        c("Generate", Kd);
        c("Settlement", rg);
        c("Style", ke);
        a.addSeparator();
        a.addItem("Procgen Arcana", l(this, this.arcana))
    },
    toggleWindow: function(a) {
        var b =
            u.findWidnow(a);
        null == b ? (a = w.createInstance(a, []), u.showDialog(a), a instanceof vc && va.__cast(a, vc).restore()) : b.hide()
    },
    addLandmark: function() {
        "Hidden" == ba.get("landmarks") && (ba.set("landmarks", "Icon"), this.toggleOverlays());
        u.showDialog(new hh(this.model.addLandmark(this.mouse.clone())))
    },
    buildNew: function() {
        new Ub(Fd.create(Ub.nextSize, C.seed));
        bb.switchScene(Ec)
    },
    toggleDistricts: function() {
        switch (ba.get("districts", "Curved")) {
            case "Curved":
                var a = "Legend";
                break;
            case "Hidden":
                a = "Straight";
                break;
            case "Legend":
                a = "Hidden";
                break;
            default:
                a = "Curved"
        }
        ba.set("districts", a);
        this.toggleOverlays();
        a = 0;
        for (var b = this.model.districts; a < b.length;) {
            var c = b[a];
            ++a;
            c.updateGeometry()
        }
        this.layoutLabels()
    },
    toggleGrid: function() {
        ba.set("grid", !ba.get("grid", !0));
        this.toggleOverlays()
    },
    toggleBuildings: function() {
        "Hidden" == kc.planMode ? kc.planMode = Ec.toggleBuildings_displayMode : (Ec.toggleBuildings_displayMode = kc.planMode, kc.planMode = "Hidden");
        ba.set("display_mode", kc.planMode);
        ia.inst.drawMap()
    },
    toggleAlleys: function() {
        ba.set("show_alleys",
            !ba.get("show_alleys", !1));
        ia.map.updateRoads()
    },
    toggleThinLines: function() {
        ba.set("thin_lines", K.thinLines = !K.thinLines);
        bb.switchScene(Ec)
    },
    showStyleTab: function(a) {
        var b = u.findWidnow(ke);
        null == b ? (b = new ke, u.showDialog(b), b.restore()) : b = b.content;
        b.tabs.onTab(a)
    },
    showElements: function() {
        this.showStyleTab(1)
    },
    showBuildings: function() {
        this.showStyleTab(2)
    },
    showOutlines: function() {
        this.showStyleTab(3)
    },
    arcana: function() {
        var a = new If("https://watabou.github.io/");
        Ra.navigateToURL(a, "arcana")
    },
    __class__: Ec
});
var jg = function(a, b) {
    null == b && (b = 0);
    null == a && (a = 0);
    this.x = a;
    this.y = b
};
g["lime.math.Vector2"] = jg;
jg.__name__ = "lime.math.Vector2";
jg.prototype = {
    offset: function(a, b) {
        this.x += a;
        this.y += b
    },
    __toFlashPoint: function() {
        return null
    },
    __class__: jg
};
var Fb = function(a, b, c, d) {
    null == d && (d = -1);
    null == c && (c = !0);
    this.__drawableType = 0;
    this.transparent = c;
    null == a && (a = 0);
    null == b && (b = 0);
    0 > a && (a = 0);
    0 > b && (b = 0);
    this.width = a;
    this.height = b;
    this.rect = new na(0, 0, a, b);
    this.__textureWidth = a;
    this.__textureHeight = b;
    0 < a && 0 < b && (c ? 0 ==
        (d & -16777216) && (d = 0) : d = -16777216 | d & 16777215, this.image = new Xb(null, 0, 0, a, b, d << 8 | d >>> 24 & 255), this.image.set_transparent(c), this.readable = this.__isValid = !0);
    this.__renderTransform = new ua;
    this.__worldAlpha = 1;
    this.__worldTransform = new ua;
    this.__worldColorTransform = new Tb;
    this.__renderable = !0
};
g["openfl.display.BitmapData"] = Fb;
Fb.__name__ = "openfl.display.BitmapData";
Fb.__interfaces__ = [mb];
Fb.fromCanvas = function(a, b) {
    null == b && (b = !0);
    if (null == a) return null;
    var c = new Fb(0, 0, b, 0);
    c.__fromImage(Xb.fromCanvas(a));
    c.image.set_transparent(b);
    return c
};
Fb.fromImage = function(a, b) {
    null == b && (b = !0);
    if (null == a || null == a.buffer) return null;
    var c = new Fb(0, 0, b, 0);
    c.__fromImage(a);
    c.image.set_transparent(b);
    return null != c.image ? c : null
};
Fb.loadFromFile = function(a) {
    return Xb.loadFromFile(a).then(function(a) {
        return hc.withValue(Fb.fromImage(a))
    })
};
Fb.prototype = {
    colorTransform: function(a, b) {
        this.readable && this.image.colorTransform(a.__toLimeRectangle(), b.__toLimeColorMatrix())
    },
    copyPixels: function(a, b, c, d, f, h) {
        null == h && (h = !1);
        this.readable && null != a && (null != f && (Fb.__tempVector.x = f.x, Fb.__tempVector.y = f.y), this.image.copyPixels(a.image, b.__toLimeRectangle(), c.__toLimeVector2(), null != d ? d.image : null, null != f ? Fb.__tempVector : null, h))
    },
    dispose: function() {
        this.image = null;
        this.height = this.width = 0;
        this.rect = null;
        this.readable = this.__isValid = !1;
        this.__textureContext = this.__texture = this.__framebufferContext = this.__framebuffer = this.__vertexBuffer = this.__surface = null
    },
    draw: function(a, b, c, d, f, h) {
        null == h && (h = !1);
        if (null != a) {
            var k = !0,
                n = null;
            a instanceof S && (n = va.__cast(a, S), n.get_visible() || (k = !1, n.set_visible(!0)));
            a.__update(!1, !0);
            var p = ua.__pool.get();
            p.copyFrom(a.__renderTransform);
            p.invert();
            null != b && p.concat(b);
            b = null;
            null != f && (b = ua.__pool.get(), b.copyFrom(p), b.invert());
            var g = new Tb;
            g.__copyFrom(a.__worldColorTransform);
            g.__invert();
            if (this.readable || null == Ra.get_current().stage.context3D) {
                if (null != c) {
                    var q = na.__pool.get(),
                        m = ua.__pool.get();
                    a.__getBounds(q, m);
                    var u = Math.ceil(q.width),
                        r = Math.ceil(q.height);
                    m.tx = -q.x;
                    m.ty = -q.y;
                    u = new Fb(u, r, !0, 0);
                    u.draw(a, m);
                    u.colorTransform(u.rect, c);
                    u.__renderTransform.identity();
                    u.__renderTransform.tx = q.x;
                    u.__renderTransform.ty = q.y;
                    u.__renderTransform.concat(a.__renderTransform);
                    u.__worldAlpha = a.__worldAlpha;
                    u.__worldColorTransform.__copyFrom(a.__worldColorTransform);
                    a = u;
                    na.__pool.release(q);
                    ua.__pool.release(m)
                }
                Ka.convertToCanvas(this.image);
                c = new Ue(this.image.buffer.__srcContext);
                c.__allowSmoothing = h;
                c.__overrideBlendMode = d;
                c.__worldTransform = p;
                c.__worldAlpha = 1 / a.__worldAlpha;
                c.__worldColorTransform = g;
                null != f && c.__pushMaskRect(f, b);
                this.__drawCanvas(a, c)
            } else null == this.__textureContext && (this.__textureContext = A.current.__window.context), null != c && g.__combine(c), c = new db(Ra.get_current().stage.context3D, this), c.__allowSmoothing = h, c.__pixelRatio = Ra.get_current().stage.window.__scale, c.__overrideBlendMode = d, c.__worldTransform = p, c.__worldAlpha = 1 / a.__worldAlpha, c.__worldColorTransform = g, c.__resize(this.width, this.height), null != f && c.__pushMaskRect(f, b), this.__drawGL(a, c);
            null !=
                f && (c.__popMaskRect(), ua.__pool.release(b));
            ua.__pool.release(p);
            null == n || k || n.set_visible(!1)
        }
    },
    encode: function(a, b, c) {
        if (!this.readable || null == a) return null;
        null == c && (c = new Vc(0));
        var d = this.image;
        if (!a.equals(this.rect)) {
            var f = ua.__pool.get();
            f.tx = Math.round(-a.x);
            f.ty = Math.round(-a.y);
            a = new Fb(Math.ceil(a.width), Math.ceil(a.height), !0, 0);
            a.draw(this, f);
            d = a.image;
            ua.__pool.release(f)
        }
        return b instanceof ri ? (c.writeBytes(Td.fromBytes(d.encode(si.PNG)), 0, 0), c) : b instanceof ti ? (c.writeBytes(Td.fromBytes(d.encode(si.JPEG,
            va.__cast(b, ti).quality)), 0, 0), c) : null
    },
    fillRect: function(a, b) {
        this.__fillRect(a, b, !0)
    },
    getIndexBuffer: function(a, b) {
        if (null == this.__indexBuffer || this.__indexBufferContext != a.__context || null != b && null == this.__indexBufferGrid || null != this.__indexBufferGrid && !this.__indexBufferGrid.equals(b)) {
            this.__indexBufferContext = a.__context;
            this.__indexBuffer = null;
            if (null != b) {
                null == this.__indexBufferGrid && (this.__indexBufferGrid = new na);
                this.__indexBufferGrid.copyFrom(b);
                var c = b.width;
                b = b.height;
                0 != c && 0 != b ? (this.__indexBufferData =
                    new Uint16Array(54), this.__indexBufferData[0] = 0, this.__indexBufferData[1] = 1, this.__indexBufferData[2] = 2, this.__indexBufferData[3] = 2, this.__indexBufferData[4] = 1, this.__indexBufferData[5] = 3, this.__indexBufferData[6] = 4, this.__indexBufferData[7] = 0, this.__indexBufferData[8] = 5, this.__indexBufferData[9] = 5, this.__indexBufferData[10] = 0, this.__indexBufferData[11] = 2, this.__indexBufferData[12] = 6, this.__indexBufferData[13] = 4, this.__indexBufferData[14] = 7, this.__indexBufferData[15] = 7, this.__indexBufferData[16] =
                    4, this.__indexBufferData[17] = 5, this.__indexBufferData[18] = 8, this.__indexBufferData[19] = 9, this.__indexBufferData[20] = 0, this.__indexBufferData[21] = 0, this.__indexBufferData[22] = 9, this.__indexBufferData[23] = 1, this.__indexBufferData[24] = 10, this.__indexBufferData[25] = 8, this.__indexBufferData[26] = 4, this.__indexBufferData[27] = 4, this.__indexBufferData[28] = 8, this.__indexBufferData[29] = 0, this.__indexBufferData[30] = 11, this.__indexBufferData[31] = 10, this.__indexBufferData[32] = 6, this.__indexBufferData[33] = 6, this.__indexBufferData[34] =
                    10, this.__indexBufferData[35] = 4, this.__indexBufferData[36] = 12, this.__indexBufferData[37] = 13, this.__indexBufferData[38] = 8, this.__indexBufferData[39] = 8, this.__indexBufferData[40] = 13, this.__indexBufferData[41] = 9, this.__indexBufferData[42] = 14, this.__indexBufferData[43] = 12, this.__indexBufferData[44] = 10, this.__indexBufferData[45] = 10, this.__indexBufferData[46] = 12, this.__indexBufferData[47] = 8, this.__indexBufferData[48] = 15, this.__indexBufferData[49] = 14, this.__indexBufferData[50] = 11, this.__indexBufferData[51] =
                    11, this.__indexBufferData[52] = 14, this.__indexBufferData[53] = 10, this.__indexBuffer = a.createIndexBuffer(54)) : 0 == c && 0 != b ? (this.__indexBufferData = new Uint16Array(18), this.__indexBufferData[0] = 0, this.__indexBufferData[1] = 1, this.__indexBufferData[2] = 2, this.__indexBufferData[3] = 2, this.__indexBufferData[4] = 1, this.__indexBufferData[5] = 3, this.__indexBufferData[6] = 4, this.__indexBufferData[7] = 5, this.__indexBufferData[8] = 0, this.__indexBufferData[9] = 0, this.__indexBufferData[10] = 5, this.__indexBufferData[11] = 1, this.__indexBufferData[12] =
                    6, this.__indexBufferData[13] = 7, this.__indexBufferData[14] = 4, this.__indexBufferData[15] = 4, this.__indexBufferData[16] = 7, this.__indexBufferData[17] = 5, this.__indexBuffer = a.createIndexBuffer(18)) : 0 != c && 0 == b && (this.__indexBufferData = new Uint16Array(18), this.__indexBufferData[0] = 0, this.__indexBufferData[1] = 1, this.__indexBufferData[2] = 2, this.__indexBufferData[3] = 2, this.__indexBufferData[4] = 1, this.__indexBufferData[5] = 3, this.__indexBufferData[6] = 4, this.__indexBufferData[7] = 0, this.__indexBufferData[8] = 5, this.__indexBufferData[9] =
                    5, this.__indexBufferData[10] = 0, this.__indexBufferData[11] = 2, this.__indexBufferData[12] = 6, this.__indexBufferData[13] = 4, this.__indexBufferData[14] = 7, this.__indexBufferData[15] = 7, this.__indexBufferData[16] = 4, this.__indexBufferData[17] = 5, this.__indexBuffer = a.createIndexBuffer(18))
            } else this.__indexBufferGrid = null;
            null == this.__indexBuffer && (this.__indexBufferData = new Uint16Array(6), this.__indexBufferData[0] = 0, this.__indexBufferData[1] = 1, this.__indexBufferData[2] = 2, this.__indexBufferData[3] = 2, this.__indexBufferData[4] =
                1, this.__indexBufferData[5] = 3, this.__indexBuffer = a.createIndexBuffer(6));
            this.__indexBuffer.uploadFromTypedArray(this.__indexBufferData)
        }
        return this.__indexBuffer
    },
    getVertexBuffer: function(a, b, c) {
        if (null == this.__vertexBuffer || this.__vertexBufferContext != a.__context || null != b && null == this.__vertexBufferGrid || null != this.__vertexBufferGrid && !this.__vertexBufferGrid.equals(b) || null != c && (this.__vertexBufferWidth != c.get_width() || this.__vertexBufferHeight != c.get_height() || this.__vertexBufferScaleX != c.get_scaleX() ||
                this.__vertexBufferScaleY != c.get_scaleY())) {
            this.__uvRect = new na(0, 0, this.width, this.height);
            this.__vertexBufferContext = a.__context;
            this.__vertexBuffer = null;
            null != c && (this.__vertexBufferWidth = c.get_width(), this.__vertexBufferHeight = c.get_height(), this.__vertexBufferScaleX = c.get_scaleX(), this.__vertexBufferScaleY = c.get_scaleY());
            if (null != b && null != c) {
                null == this.__vertexBufferGrid && (this.__vertexBufferGrid = new na);
                this.__vertexBufferGrid.copyFrom(b);
                this.__vertexBufferWidth = c.get_width();
                this.__vertexBufferHeight =
                    c.get_height();
                this.__vertexBufferScaleX = c.get_scaleX();
                this.__vertexBufferScaleY = c.get_scaleY();
                var d = b.width,
                    f = b.height;
                if (0 != d && 0 != f) {
                    this.__vertexBufferData = new Float32Array(224);
                    var h = b.x,
                        k = b.y,
                        n = this.width - d - h,
                        p = this.height - f - k;
                    b = h / this.width;
                    var g = k / this.height;
                    d /= this.width;
                    f /= this.height;
                    h /= c.get_scaleX();
                    k /= c.get_scaleY();
                    n /= c.get_scaleX();
                    var q = p / c.get_scaleY();
                    p = c.get_width() / c.get_scaleX() - h - n;
                    n = c.get_height() / c.get_scaleY() - k - q;
                    this.__vertexBufferData[0] = h;
                    this.__vertexBufferData[1] =
                        k;
                    this.__vertexBufferData[3] = 1 * b;
                    this.__vertexBufferData[4] = 1 * g;
                    this.__vertexBufferData[15] = k;
                    this.__vertexBufferData[18] = 1 * g;
                    this.__vertexBufferData[28] = h;
                    this.__vertexBufferData[31] = 1 * b;
                    this.__vertexBufferData[56] = h + p;
                    this.__vertexBufferData[57] = k;
                    this.__vertexBufferData[59] = 1 * (b + d);
                    this.__vertexBufferData[60] = 1 * g;
                    this.__vertexBufferData[70] = h + p;
                    this.__vertexBufferData[73] = 1 * (b + d);
                    this.__vertexBufferData[84] = this.width;
                    this.__vertexBufferData[85] = k;
                    this.__vertexBufferData[87] = 1;
                    this.__vertexBufferData[88] =
                        1 * g;
                    this.__vertexBufferData[98] = this.width;
                    this.__vertexBufferData[101] = 1;
                    this.__vertexBufferData[112] = h;
                    this.__vertexBufferData[113] = k + n;
                    this.__vertexBufferData[115] = 1 * b;
                    this.__vertexBufferData[116] = 1 * (g + f);
                    this.__vertexBufferData[127] = k + n;
                    this.__vertexBufferData[130] = 1 * (g + f);
                    this.__vertexBufferData[140] = h + p;
                    this.__vertexBufferData[141] = k + n;
                    this.__vertexBufferData[143] = 1 * (b + d);
                    this.__vertexBufferData[144] = 1 * (g + f);
                    this.__vertexBufferData[154] = this.width;
                    this.__vertexBufferData[155] = k + n;
                    this.__vertexBufferData[157] =
                        1;
                    this.__vertexBufferData[158] = 1 * (g + f);
                    this.__vertexBufferData[168] = h;
                    this.__vertexBufferData[169] = this.height;
                    this.__vertexBufferData[171] = 1 * b;
                    this.__vertexBufferData[172] = 1;
                    this.__vertexBufferData[183] = this.height;
                    this.__vertexBufferData[186] = 1;
                    this.__vertexBufferData[196] = h + p;
                    this.__vertexBufferData[197] = this.height;
                    this.__vertexBufferData[199] = 1 * (b + d);
                    this.__vertexBufferData[200] = 1;
                    this.__vertexBufferData[210] = this.width;
                    this.__vertexBufferData[211] = this.height;
                    this.__vertexBufferData[213] = 1;
                    this.__vertexBufferData[214] = 1;
                    this.__vertexBuffer = a.createVertexBuffer(16, 14)
                } else 0 == d && 0 != f ? (this.__vertexBufferData = new Float32Array(112), k = b.y, p = this.height - f - k, g = k / this.height, f /= this.height, k /= c.get_scaleY(), q = p / c.get_scaleY(), n = c.get_height() / c.get_scaleY() - k - q, c = c.get_width() / c.get_scaleX(), this.__vertexBufferData[0] = c, this.__vertexBufferData[1] = k, this.__vertexBufferData[3] = 1, this.__vertexBufferData[4] = 1 * g, this.__vertexBufferData[15] = k, this.__vertexBufferData[18] = 1 * g, this.__vertexBufferData[28] =
                    c, this.__vertexBufferData[31] = 1, this.__vertexBufferData[56] = c, this.__vertexBufferData[57] = k + n, this.__vertexBufferData[59] = 1, this.__vertexBufferData[60] = 1 * (g + f), this.__vertexBufferData[71] = k + n, this.__vertexBufferData[74] = 1 * (g + f), this.__vertexBufferData[84] = c, this.__vertexBufferData[85] = this.height, this.__vertexBufferData[87] = 1, this.__vertexBufferData[88] = 1, this.__vertexBufferData[99] = this.height, this.__vertexBufferData[102] = 1, this.__vertexBuffer = a.createVertexBuffer(8, 14)) : 0 == f && 0 != d && (this.__vertexBufferData =
                    new Float32Array(112), h = b.x, n = this.width - d - h, b = h / this.width, d /= this.width, h /= c.get_scaleX(), n /= c.get_scaleX(), p = c.get_width() / c.get_scaleX() - h - n, c = c.get_height() / c.get_scaleY(), this.__vertexBufferData[0] = h, this.__vertexBufferData[1] = c, this.__vertexBufferData[3] = 1 * b, this.__vertexBufferData[4] = 1, this.__vertexBufferData[15] = c, this.__vertexBufferData[18] = 1, this.__vertexBufferData[28] = h, this.__vertexBufferData[31] = 1 * b, this.__vertexBufferData[56] = h + p, this.__vertexBufferData[57] = c, this.__vertexBufferData[59] =
                    1 * (b + d), this.__vertexBufferData[60] = 1, this.__vertexBufferData[70] = h + p, this.__vertexBufferData[73] = 1 * (b + d), this.__vertexBufferData[84] = this.width, this.__vertexBufferData[85] = c, this.__vertexBufferData[87] = 1, this.__vertexBufferData[88] = 1, this.__vertexBufferData[98] = this.width, this.__vertexBufferData[101] = 1, this.__vertexBuffer = a.createVertexBuffer(8, 14))
            } else this.__vertexBufferGrid = null;
            null == this.__vertexBuffer && (this.__vertexBufferData = new Float32Array(56), this.__vertexBufferData[0] = this.width, this.__vertexBufferData[1] =
                this.height, this.__vertexBufferData[3] = 1, this.__vertexBufferData[4] = 1, this.__vertexBufferData[15] = this.height, this.__vertexBufferData[18] = 1, this.__vertexBufferData[28] = this.width, this.__vertexBufferData[31] = 1, this.__vertexBuffer = a.createVertexBuffer(3, 14));
            this.__vertexBuffer.uploadFromTypedArray(ih.toArrayBufferView(this.__vertexBufferData))
        }
        return this.__vertexBuffer
    },
    getPixel: function(a, b) {
        return this.readable ? this.image.getPixel(a, b, 1) : 0
    },
    getTexture: function(a) {
        if (!this.__isValid) return null;
        if (null == this.__texture || this.__textureContext != a.__context) this.__textureContext = a.__context, this.__texture = a.createRectangleTexture(this.width, this.height, 1, !1), this.__textureVersion = -1;
        Ka.sync(this.image, !1);
        null != this.image && this.image.version > this.__textureVersion && (null != this.__surface && El.flush(this.__surface), a = this.image, Eb.__supportsBGRA || 0 == a.get_format() || (a = a.clone(), a.set_format(0)), this.__texture.__uploadFromImage(a), this.__textureVersion = this.image.version, this.__textureWidth = a.buffer.width,
            this.__textureHeight = a.buffer.height);
        this.readable || null == this.image || (this.image = this.__surface = null);
        return this.__texture
    },
    setPixel: function(a, b, c) {
        this.readable && this.image.setPixel(a, b, c, 1)
    },
    __drawCanvas: function(a, b) {
        var c = this.image.buffer;
        b.__allowSmoothing || b.applySmoothing(c.__srcContext, !1);
        b.__render(a);
        b.__allowSmoothing || b.applySmoothing(c.__srcContext, !0);
        c.__srcContext.setTransform(1, 0, 0, 1, 0, 0);
        c.__srcImageData = null;
        c.data = null;
        this.image.dirty = !0;
        this.image.version++
    },
    __drawGL: function(a,
        b) {
        var c = b.__context3D,
            d = c.__state.renderToTexture,
            f = c.__state.renderToTextureDepthStencil,
            h = c.__state.renderToTextureAntiAlias,
            k = c.__state.renderToTextureSurfaceSelector;
        c.setRenderToTexture(this.getTexture(c), !0);
        b.__render(a);
        null != d ? c.setRenderToTexture(d, f, h, k) : c.setRenderToBackBuffer()
    },
    __fillRect: function(a, b, c) {
        if (null != a)
            if (this.transparent && 0 == (b & -16777216) && (b = 0), c && null != this.__texture && null != this.__texture.__glFramebuffer && "opengl" == Ra.get_current().stage.__renderer.__type) {
                c = Ra.get_current().stage.__renderer.__context3D;
                var d = !this.rect.equals(a),
                    f = c.__state.renderToTexture,
                    h = c.__state.renderToTextureDepthStencil,
                    k = c.__state.renderToTextureAntiAlias,
                    n = c.__state.renderToTextureSurfaceSelector;
                c.setRenderToTexture(this.__texture);
                d && c.setScissorRectangle(a);
                c.clear((b >>> 16 & 255) / 255, (b >>> 8 & 255) / 255, (b & 255) / 255, this.transparent ? (b >>> 24 & 255) / 255 : 1, 0, 0, 1);
                d && c.setScissorRectangle(null);
                null != f ? c.setRenderToTexture(f, h, k, n) : c.setRenderToBackBuffer()
            } else this.readable && this.image.fillRect(a.__toLimeRectangle(), b, 1)
    },
    __fromImage: function(a) {
        null !=
            a && null != a.buffer && (this.image = a, this.width = a.width, this.height = a.height, this.rect = new na(0, 0, a.width, a.height), this.__textureWidth = this.width, this.__textureHeight = this.height, this.__isValid = this.readable = !0)
    },
    __getBounds: function(a, b) {
        var c = na.__pool.get();
        this.rect.__transform(c, b);
        a.__expand(c.x, c.y, c.width, c.height);
        na.__pool.release(c)
    },
    __setUVRect: function(a, b, c, d, f) {
        if (null != this.getVertexBuffer(a) && (d != this.__uvRect.width || f != this.__uvRect.height || b != this.__uvRect.x || c != this.__uvRect.y)) {
            null ==
                this.__uvRect && (this.__uvRect = new na);
            this.__uvRect.setTo(b, c, d, f);
            a = 0 < this.__textureWidth ? b / this.__textureWidth : 0;
            c = 0 < this.__textureHeight ? c / this.__textureHeight : 0;
            b = 0 < this.__textureWidth ? d / this.__textureWidth : 0;
            var h = 0 < this.__textureHeight ? f / this.__textureHeight : 0;
            this.__vertexBufferData[0] = d;
            this.__vertexBufferData[1] = f;
            this.__vertexBufferData[3] = a + b;
            this.__vertexBufferData[4] = c + h;
            this.__vertexBufferData[15] = f;
            this.__vertexBufferData[17] = a;
            this.__vertexBufferData[18] = c + h;
            this.__vertexBufferData[28] =
                d;
            this.__vertexBufferData[31] = a + b;
            this.__vertexBufferData[32] = c;
            this.__vertexBufferData[45] = a;
            this.__vertexBufferData[46] = c;
            this.__vertexBuffer.uploadFromTypedArray(ih.toArrayBufferView(this.__vertexBufferData))
        }
    },
    __update: function(a, b) {
        this.__updateTransforms()
    },
    __updateTransforms: function(a) {
        null == a ? this.__worldTransform.identity() : this.__worldTransform.copyFrom(a);
        this.__renderTransform.copyFrom(this.__worldTransform)
    },
    __class__: Fb
};
var na = function(a, b, c, d) {
    null == d && (d = 0);
    null == c && (c = 0);
    null ==
        b && (b = 0);
    null == a && (a = 0);
    this.x = a;
    this.y = b;
    this.width = c;
    this.height = d
};
g["openfl.geom.Rectangle"] = na;
na.__name__ = "openfl.geom.Rectangle";
na.prototype = {
    clone: function() {
        return new na(this.x, this.y, this.width, this.height)
    },
    contains: function(a, b) {
        return a >= this.x && b >= this.y && a < this.get_right() ? b < this.get_bottom() : !1
    },
    containsPoint: function(a) {
        return this.contains(a.x, a.y)
    },
    copyFrom: function(a) {
        this.x = a.x;
        this.y = a.y;
        this.width = a.width;
        this.height = a.height
    },
    equals: function(a) {
        return a == this ? !0 : null != a &&
            this.x == a.x && this.y == a.y && this.width == a.width ? this.height == a.height : !1
    },
    inflate: function(a, b) {
        this.x -= a;
        this.width += 2 * a;
        this.y -= b;
        this.height += 2 * b
    },
    intersects: function(a) {
        var b = this.x < a.x ? a.x : this.x;
        if ((this.get_right() > a.get_right() ? a.get_right() : this.get_right()) <= b) return !1;
        b = this.y < a.y ? a.y : this.y;
        return (this.get_bottom() > a.get_bottom() ? a.get_bottom() : this.get_bottom()) > b
    },
    offset: function(a, b) {
        this.x += a;
        this.y += b
    },
    setTo: function(a, b, c, d) {
        this.x = a;
        this.y = b;
        this.width = c;
        this.height = d
    },
    __contract: function(a,
        b, c, d) {
        if (0 != this.width || 0 != this.height) {
            var f = 0,
                h = 0,
                k = 0,
                n = 0;
            this.x < a && (f = a - this.x);
            this.y < b && (h = b - this.y);
            this.get_right() > a + c && (k = a + c - this.get_right());
            this.get_bottom() > b + d && (n = b + d - this.get_bottom());
            this.x += f;
            this.y += h;
            this.width += k - f;
            this.height += n - h
        }
    },
    __expand: function(a, b, c, d) {
        if (0 == this.width && 0 == this.height) this.x = a, this.y = b, this.width = c, this.height = d;
        else {
            var f = this.get_right(),
                h = this.get_bottom();
            this.x > a && (this.x = a, this.width = f - a);
            this.y > b && (this.y = b, this.height = h - b);
            f < a + c && (this.width =
                a + c - this.x);
            h < b + d && (this.height = b + d - this.y)
        }
    },
    __toLimeRectangle: function() {
        null == na.__limeRectangle && (na.__limeRectangle = new Ld);
        na.__limeRectangle.setTo(this.x, this.y, this.width, this.height);
        return na.__limeRectangle
    },
    __transform: function(a, b) {
        var c = b.a * this.x + b.c * this.y,
            d = c,
            f = b.b * this.x + b.d * this.y,
            h = f,
            k = b.a * (this.x + this.width) + b.c * this.y,
            n = b.b * (this.x + this.width) + b.d * this.y;
        k < c && (c = k);
        n < f && (f = n);
        k > d && (d = k);
        n > h && (h = n);
        k = b.a * (this.x + this.width) + b.c * (this.y + this.height);
        n = b.b * (this.x + this.width) + b.d *
            (this.y + this.height);
        k < c && (c = k);
        n < f && (f = n);
        k > d && (d = k);
        n > h && (h = n);
        k = b.a * this.x + b.c * (this.y + this.height);
        n = b.b * this.x + b.d * (this.y + this.height);
        k < c && (c = k);
        n < f && (f = n);
        k > d && (d = k);
        n > h && (h = n);
        a.setTo(c + b.tx, f + b.ty, d - c, h - f)
    },
    get_bottom: function() {
        return this.y + this.height
    },
    get_left: function() {
        return this.x
    },
    get_right: function() {
        return this.x + this.width
    },
    get_top: function() {
        return this.y
    },
    __class__: na,
    __properties__: {
        get_top: "get_top",
        get_right: "get_right",
        get_left: "get_left",
        get_bottom: "get_bottom"
    }
};
var Xb =
    function(a, b, c, d, f, h, k) {
        null == f && (f = -1);
        null == d && (d = -1);
        null == c && (c = 0);
        null == b && (b = 0);
        this.offsetX = b;
        this.offsetY = c;
        this.width = d;
        this.height = f;
        this.version = 0;
        null == k && (k = Zc.CANVAS, null != Ad.__current.__worker || Ad.__isWorker) && (k = Zc.DATA);
        this.type = k;
        if (null == a) {
            if (0 < d && 0 < f) switch (this.type._hx_index) {
                case 0:
                    this.buffer = new Ve(null, d, f);
                    Ka.createCanvas(this, d, f);
                    null != h && 0 != h && this.fillRect(new Ld(0, 0, d, f), h);
                    break;
                case 1:
                    a = d * f * 4, a = null != a ? new Uint8Array(a) : null, this.buffer = new Ve(a, d, f), null != h && 0 !=
                        h && this.fillRect(new Ld(0, 0, d, f), h)
            }
        } else this.__fromImageBuffer(a)
    };
g["lime.graphics.Image"] = Xb;
Xb.__name__ = "lime.graphics.Image";
Xb.fromCanvas = function(a) {
    if (null == a) return null;
    var b = new Ve(null, a.width, a.height);
    b.set_src(a);
    a = new Xb(b);
    a.type = Zc.CANVAS;
    return a
};
Xb.fromFile = function(a) {
    if (null == a) return null;
    var b = new Xb;
    return b.__fromFile(a) ? b : null
};
Xb.loadFromBytes = function(a) {
    if (null == a) return hc.withValue(null);
    if (Xb.__isPNG(a)) var b = "image/png";
    else if (Xb.__isJPG(a)) b = "image/jpeg";
    else if (Xb.__isGIF(a)) b =
        "image/gif";
    else if (Xb.__isWebP(a)) b = "image/webp";
    else return hc.withValue(null);
    return Ba.loadImageFromBytes(a, b)
};
Xb.loadFromFile = function(a) {
    return null == a ? hc.withValue(null) : Ba.loadImage(a)
};
Xb.__isGIF = function(a) {
    if (null == a || 6 > a.length) return !1;
    a = a.getString(0, 6);
    return "GIF87a" != a ? "GIF89a" == a : !0
};
Xb.__isJPG = function(a) {
    return null == a || 4 > a.length ? !1 : 255 == a.b[0] && 216 == a.b[1] && 255 == a.b[a.length - 2] ? 217 == a.b[a.length - 1] : !1
};
Xb.__isPNG = function(a) {
    return null == a || 8 > a.length ? !1 : 137 == a.b[0] && 80 == a.b[1] &&
        78 == a.b[2] && 71 == a.b[3] && 13 == a.b[4] && 10 == a.b[5] && 26 == a.b[6] ? 10 == a.b[7] : !1
};
Xb.__isWebP = function(a) {
    return null == a || 16 > a.length ? !1 : "RIFF" == a.getString(0, 4) ? "WEBP" == a.getString(8, 4) : !1
};
Xb.prototype = {
    clone: function() {
        if (null != this.buffer) {
            this.type == Zc.CANVAS ? Ka.convertToCanvas(this) : Ka.convertToData(this);
            var a = new Xb(this.buffer.clone(), this.offsetX, this.offsetY, this.width, this.height, null, this.type);
            a.version = this.version;
            return a
        }
        return new Xb(null, this.offsetX, this.offsetY, this.width, this.height,
            null, this.type)
    },
    colorTransform: function(a, b) {
        a = this.__clipRect(a);
        if (null != this.buffer && null != a) switch (this.type._hx_index) {
            case 0:
                Ka.colorTransform(this, a, b);
                break;
            case 1:
                Ka.convertToData(this);
                bc.colorTransform(this, a, b);
                break;
            case 2:
                a.offset(this.offsetX, this.offsetY), this.buffer.__srcBitmapData.colorTransform(a.__toFlashRectangle(), Qc.__toFlashColorTransform(b))
        }
    },
    copyChannel: function(a, b, c, d, f) {
        b = this.__clipRect(b);
        if (null != this.buffer && null != b && !(f == sg.ALPHA && !this.get_transparent() || 0 >= b.width ||
                0 >= b.height)) switch (b.x + b.width > a.width && (b.width = a.width - b.x), b.y + b.height > a.height && (b.height = a.height - b.y), this.type._hx_index) {
            case 0:
                Ka.copyChannel(this, a, b, c, d, f);
                break;
            case 1:
                Ka.convertToData(this);
                Ka.convertToData(a);
                bc.copyChannel(this, a, b, c, d, f);
                break;
            case 2:
                switch (d._hx_index) {
                    case 0:
                        var h = 1;
                        break;
                    case 1:
                        h = 2;
                        break;
                    case 2:
                        h = 4;
                        break;
                    case 3:
                        h = 8
                }
                switch (f._hx_index) {
                    case 0:
                        var k = 1;
                        break;
                    case 1:
                        k = 2;
                        break;
                    case 2:
                        k = 4;
                        break;
                    case 3:
                        k = 8
                }
                b.offset(a.offsetX, a.offsetY);
                c.offset(this.offsetX, this.offsetY);
                this.buffer.__srcBitmapData.copyChannel(a.buffer.get_src(), b.__toFlashRectangle(), c.__toFlashPoint(), h, k)
        }
    },
    copyPixels: function(a, b, c, d, f, h) {
        null == h && (h = !1);
        if (null != this.buffer && null != a && !(0 >= b.width || 0 >= b.height || 0 >= this.width || 0 >= this.height)) switch (b.x + b.width > a.width && (b.width = a.width - b.x), b.y + b.height > a.height && (b.height = a.height - b.y), 0 > b.x && (b.width += b.x, b.x = 0), 0 > b.y && (b.height += b.y, b.y = 0), c.x + b.width > this.width && (b.width = this.width - c.x), c.y + b.height > this.height && (b.height = this.height - c.y),
            0 > c.x && (b.width += c.x, b.x -= c.x, c.x = 0), 0 > c.y && (b.height += c.y, b.y -= c.y, c.y = 0), a == this && c.x < b.get_right() && c.y < b.get_bottom() && (a = this.clone()), d == a && (null == f || 0 == f.x && 0 == f.y) && (f = d = null), this.type._hx_index) {
            case 0:
                null != d ? (Ka.convertToData(this), Ka.convertToData(a), null != d && Ka.convertToData(d), bc.copyPixels(this, a, b, c, d, f, h)) : (Ka.convertToCanvas(this), Ka.convertToCanvas(a), Ka.copyPixels(this, a, b, c, d, f, h));
                break;
            case 1:
                Ka.convertToData(this);
                Ka.convertToData(a);
                null != d && Ka.convertToData(d);
                bc.copyPixels(this,
                    a, b, c, d, f, h);
                break;
            case 2:
                b.offset(a.offsetX, a.offsetY), c.offset(this.offsetX, this.offsetY), null != d && null != f && f.offset(d.offsetX, d.offsetY), this.buffer.__srcBitmapData.copyPixels(a.buffer.__srcBitmapData, b.__toFlashRectangle(), c.__toFlashPoint(), null != d ? d.buffer.get_src() : null, null != f ? f.__toFlashPoint() : null, h)
        }
    },
    encode: function(a, b) {
        null == b && (b = 90);
        if (null == a) return kk.encode(this);
        switch (a._hx_index) {
            case 0:
                return al.encode(this);
            case 1:
                return bl.encode(this, b);
            case 2:
                return kk.encode(this)
        }
    },
    fillRect: function(a,
        b, c) {
        a = this.__clipRect(a);
        if (null != this.buffer && null != a) switch (this.type._hx_index) {
            case 0:
                Ka.fillRect(this, a, b, c);
                break;
            case 1:
                Ka.convertToData(this);
                if (0 == this.buffer.data.length) break;
                bc.fillRect(this, a, b, c);
                break;
            case 2:
                a.offset(this.offsetX, this.offsetY);
                if (null == c) b = (b & 255) << 24 | (b >>> 24 & 255) << 16 | (b >>> 16 & 255) << 8 | b >>> 8 & 255;
                else switch (c) {
                    case 1:
                        break;
                    case 2:
                        b = (b & 255) << 24 | (b >>> 8 & 255) << 16 | (b >>> 16 & 255) << 8 | b >>> 24 & 255;
                        break;
                    default:
                        b = (b & 255) << 24 | (b >>> 24 & 255) << 16 | (b >>> 16 & 255) << 8 | b >>> 8 & 255
                }
                this.buffer.__srcBitmapData.fillRect(a.__toFlashRectangle(),
                    b)
        }
    },
    getPixel: function(a, b, c) {
        if (null == this.buffer || 0 > a || 0 > b || a >= this.width || b >= this.height) return 0;
        switch (this.type._hx_index) {
            case 0:
                return Ka.getPixel(this, a, b, c);
            case 1:
                return Ka.convertToData(this), bc.getPixel(this, a, b, c);
            case 2:
                a = this.buffer.__srcBitmapData.getPixel(a + this.offsetX, b + this.offsetY);
                if (null == c) return (a >>> 16 & 255) << 24 | (a >>> 8 & 255) << 16 | (a & 255) << 8 | a >>> 24 & 255;
                switch (c) {
                    case 1:
                        return a;
                    case 2:
                        return (a & 255) << 24 | (a >>> 8 & 255) << 16 | (a >>> 16 & 255) << 8 | a >>> 24 & 255;
                    default:
                        return (a >>> 16 & 255) << 24 |
                            (a >>> 8 & 255) << 16 | (a & 255) << 8 | a >>> 24 & 255
                }
            default:
                return 0
        }
    },
    getPixels: function(a, b) {
        if (null == this.buffer) return null;
        switch (this.type._hx_index) {
            case 0:
                return Ka.getPixels(this, a, b);
            case 1:
                return Ka.convertToData(this), bc.getPixels(this, a, b);
            case 2:
                return null;
            default:
                return null
        }
    },
    setPixel: function(a, b, c, d) {
        if (!(null == this.buffer || 0 > a || 0 > b || a >= this.width || b >= this.height)) switch (this.type._hx_index) {
            case 0:
                Ka.setPixel(this, a, b, c, d);
                break;
            case 1:
                Ka.convertToData(this);
                bc.setPixel(this, a, b, c, d);
                break;
            case 2:
                if (null == d) c = (c & 255) << 24 | (c >>> 24 & 255) << 16 | (c >>> 16 & 255) << 8 | c >>> 8 & 255;
                else switch (d) {
                    case 1:
                        break;
                    case 2:
                        c = (c & 255) << 24 | (c >>> 8 & 255) << 16 | (c >>> 16 & 255) << 8 | c >>> 24 & 255;
                        break;
                    default:
                        c = (c & 255) << 24 | (c >>> 24 & 255) << 16 | (c >>> 16 & 255) << 8 | c >>> 8 & 255
                }
                this.buffer.__srcBitmapData.setPixel(a + this.offsetX, b + this.offsetX, c)
        }
    },
    __clipRect: function(a) {
        return null == a || 0 > a.x && (a.width -= -a.x, a.x = 0, 0 >= a.x + a.width) || 0 > a.y && (a.height -= -a.y, a.y = 0, 0 >= a.y + a.height) || a.x + a.width >= this.width && (a.width -= a.x + a.width - this.width, 0 >=
            a.width) || a.y + a.height >= this.height && (a.height -= a.y + a.height - this.height, 0 >= a.height) ? null : a
    },
    __fromBase64: function(a, b, c) {
        var d = this,
            f = new window.Image;
        f.addEventListener("load", function(a) {
            d.buffer = new Ve(null, f.width, f.height);
            d.buffer.__srcImage = f;
            d.offsetX = 0;
            d.offsetY = 0;
            d.width = d.buffer.width;
            d.height = d.buffer.height;
            null != c && c(d)
        }, !1);
        f.src = "data:" + b + ";base64," + a
    },
    __fromBytes: function(a, b) {
        if (Xb.__isPNG(a)) var c = "image/png";
        else if (Xb.__isJPG(a)) c = "image/jpeg";
        else if (Xb.__isGIF(a)) c = "image/gif";
        else return !1;
        this.__fromBase64(Be.encode(a), c, b);
        return !0
    },
    __fromFile: function(a, b, c) {
        var d = this,
            f = new window.Image;
        Ba.__isSameOrigin(a) || (f.crossOrigin = "Anonymous");
        f.onload = function(a) {
            d.buffer = new Ve(null, f.width, f.height);
            d.buffer.__srcImage = f;
            d.width = f.width;
            d.height = f.height;
            null != b && b(d)
        };
        f.onerror = function(a) {
            null != c && c()
        };
        f.src = a;
        return !0
    },
    __fromImageBuffer: function(a) {
        this.buffer = a;
        null != a && (-1 == this.width && (this.width = a.width), -1 == this.height && (this.height = a.height))
    },
    get_data: function() {
        null ==
            this.buffer.data && 0 < this.buffer.width && 0 < this.buffer.height && Ka.convertToData(this);
        return this.buffer.data
    },
    get_format: function() {
        return this.buffer.format
    },
    set_format: function(a) {
        this.buffer.format != a && 1 == this.type._hx_index && bc.setFormat(this, a);
        return this.buffer.format = a
    },
    get_premultiplied: function() {
        return this.buffer.premultiplied
    },
    set_premultiplied: function(a) {
        if (a && !this.buffer.premultiplied) switch (this.type._hx_index) {
            case 0:
            case 1:
                Ka.convertToData(this), bc.multiplyAlpha(this)
        } else !a && this.buffer.premultiplied &&
            1 == this.type._hx_index && (Ka.convertToData(this), bc.unmultiplyAlpha(this));
        return a
    },
    get_rect: function() {
        return new Ld(0, 0, this.width, this.height)
    },
    get_src: function() {
        null != this.buffer.__srcCanvas || null == this.buffer.data && this.type != Zc.DATA || Ka.convertToCanvas(this);
        return this.buffer.get_src()
    },
    get_transparent: function() {
        return null == this.buffer ? !1 : this.buffer.transparent
    },
    set_transparent: function(a) {
        return null == this.buffer ? !1 : this.buffer.transparent = a
    },
    __class__: Xb,
    __properties__: {
        set_transparent: "set_transparent",
        get_transparent: "get_transparent",
        get_src: "get_src",
        get_rect: "get_rect",
        set_premultiplied: "set_premultiplied",
        get_premultiplied: "get_premultiplied",
        set_format: "set_format",
        get_format: "get_format",
        get_data: "get_data"
    }
};
var Zc = y["lime.graphics.ImageType"] = {
    __ename__: "lime.graphics.ImageType",
    __constructs__: null,
    CANVAS: {
        _hx_name: "CANVAS",
        _hx_index: 0,
        __enum__: "lime.graphics.ImageType",
        toString: r
    },
    DATA: {
        _hx_name: "DATA",
        _hx_index: 1,
        __enum__: "lime.graphics.ImageType",
        toString: r
    },
    FLASH: {
        _hx_name: "FLASH",
        _hx_index: 2,
        __enum__: "lime.graphics.ImageType",
        toString: r
    },
    CUSTOM: {
        _hx_name: "CUSTOM",
        _hx_index: 3,
        __enum__: "lime.graphics.ImageType",
        toString: r
    }
};
Zc.__constructs__ = [Zc.CANVAS, Zc.DATA, Zc.FLASH, Zc.CUSTOM];
var lk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Dynamic_Void"] = lk;
lk.__name__ = "lime.app._Event_Dynamic_Void";
lk.prototype = {
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b,
            1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: lk
};
var ul = {
        disablePreserveClasses: function(a) {
            null != a && a instanceof Object && 1 != ya.field(a, "__skipPrototype__") && !(null != a.byteLength && null != a.byteOffset && null != a.buffer && a.buffer instanceof ArrayBuffer) && (a.__skipPrototype__ = !0)
        },
        restoreClasses: function(a, b) {
            null == b && (b = 2147483647 *
                Math.random() | 0, ya.field(a, "__restoreFlag__") == b && ++b);
            if (null != a && a instanceof Object && 1 != ya.field(a, "__skipPrototype__") && !(null != a.byteLength && null != a.byteOffset && null != a.buffer && a.buffer instanceof ArrayBuffer) && ya.field(a, "__restoreFlag__") != b) {
                try {
                    a.__restoreFlag__ = b
                } catch (f) {
                    Ta.lastError = f;
                    return
                }
                if (null != ya.field(a, "__prototype__")) try {
                    Object.setPrototypeOf(a, g[ya.field(a, "__prototype__")].prototype)
                } catch (f) {
                    Ta.lastError = f
                }
                var c = 0;
                for (a = Object.values(a); c < a.length;) {
                    var d = a[c];
                    ++c;
                    ul.restoreClasses(d,
                        b)
                }
            }
        }
    },
    Fl = {
        toFunction: function(a) {
            if (null != a.func) return a.func;
            if (null != a.classPath && null != a.functionName) return a.func = g[a.classPath][a.functionName], a.func;
            if (null != a.sourceCode) return a.func = (new Function("return " + a.sourceCode))(), a.func;
            throw X.thrown("Object is not a valid WorkFunction: " + H.string(a));
        }
    },
    va = function() {};
g["js.Boot"] = va;
va.__name__ = "js.Boot";
va.getClass = function(a) {
    if (null == a) return null;
    if (a instanceof Array) return Array;
    var b = a.__class__;
    if (null != b) return b;
    a = va.__nativeClassName(a);
    return null != a ? va.__resolveNativeClass(a) : null
};
va.__string_rec = function(a, b) {
    if (null == a) return "null";
    if (5 <= b.length) return "<...>";
    var c = typeof a;
    "function" == c && (a.__name__ || a.__ename__) && (c = "object");
    switch (c) {
        case "function":
            return "<function>";
        case "object":
            if (a.__enum__) {
                var d = y[a.__enum__].__constructs__[a._hx_index];
                c = d._hx_name;
                if (d.__params__) {
                    b += "\t";
                    var f = [],
                        h = 0;
                    for (d = d.__params__; h < d.length;) {
                        var k = d[h];
                        h += 1;
                        f.push(va.__string_rec(a[k], b))
                    }
                    return c + "(" + f.join(",") + ")"
                }
                return c
            }
            if (a instanceof Array) {
                c = "[";
                b += "\t";
                f = 0;
                for (h = a.length; f < h;) d = f++, c += (0 < d ? "," : "") + va.__string_rec(a[d], b);
                return c + "]"
            }
            try {
                f = a.toString
            } catch (n) {
                return Ta.lastError = n, "???"
            }
            if (null != f && f != Object.toString && "function" == typeof f && (c = a.toString(), "[object Object]" != c)) return c;
            c = "{\n";
            b += "\t";
            f = null != a.hasOwnProperty;
            h = null;
            for (h in a) f && !a.hasOwnProperty(h) || "prototype" == h || "__class__" == h || "__super__" == h || "__interfaces__" == h || "__properties__" == h || (2 != c.length && (c += ", \n"), c += b + h + " : " + va.__string_rec(a[h], b));
            b = b.substring(1);
            return c + ("\n" + b + "}");
        case "string":
            return a;
        default:
            return String(a)
    }
};
va.__interfLoop = function(a, b) {
    if (null == a) return !1;
    if (a == b) return !0;
    var c = a.__interfaces__;
    if (null != c)
        for (var d = 0, f = c.length; d < f;) {
            var h = d++;
            h = c[h];
            if (h == b || va.__interfLoop(h, b)) return !0
        }
    return va.__interfLoop(a.__super__, b)
};
va.__instanceof = function(a, b) {
    if (null == b) return !1;
    switch (b) {
        case Array:
            return a instanceof Array;
        case Gl:
            return "boolean" == typeof a;
        case Hl:
            return null != a;
        case Il:
            return "number" == typeof a;
        case vl:
            return "number" ==
                typeof a ? (a | 0) === a : !1;
        case String:
            return "string" == typeof a;
        default:
            if (null != a)
                if ("function" == typeof b) {
                    if (va.__downcastCheck(a, b)) return !0
                } else {
                    if ("object" == typeof b && va.__isNativeObj(b) && a instanceof b) return !0
                }
            else return !1;
            return b == xl && null != a.__name__ || b == yl && null != a.__ename__ ? !0 : null != a.__enum__ ? y[a.__enum__] == b : !1
    }
};
va.__downcastCheck = function(a, b) {
    return a instanceof b ? !0 : b.__isInterface__ ? va.__interfLoop(va.getClass(a), b) : !1
};
va.__cast = function(a, b) {
    if (null == a || va.__instanceof(a, b)) return a;
    throw X.thrown("Cannot cast " + H.string(a) + " to " + H.string(b));
};
va.__nativeClassName = function(a) {
    a = va.__toStr.call(a).slice(8, -1);
    return "Object" == a || "Function" == a || "Math" == a || "JSON" == a ? null : a
};
va.__isNativeObj = function(a) {
    return null != va.__nativeClassName(a)
};
va.__resolveNativeClass = function(a) {
    return E[a]
};
var Ad = function(a, b) {
    this.__href = a;
    null != b && (this.__worker = b, this.__worker.onmessage = l(this, this.dispatchMessage), this.onMessage = new lk);
    ul.disablePreserveClasses(this)
};
g["lime._internal.backend.html5.HTML5Thread"] =
    Ad;
Ad.__name__ = "lime._internal.backend.html5.HTML5Thread";
Ad.prototype = {
    dispatchMessage: function(a) {
        a = a.data;
        ul.restoreClasses(a);
        null != this.onMessage && this.onMessage.dispatch(a);
        Ad.__resolveMethods.isEmpty() ? Ad.__messages.add(a) : Ad.__resolveMethods.pop()(a)
    },
    destroy: function() {
        if (null != this.__worker) this.__worker.terminate();
        else if (Ad.__isWorker) try {
            E.close()
        } catch (a) {
            Ta.lastError = a
        }
    },
    __class__: Ad
};
var Ve = function(a, b, c, d, f) {
    null == d && (d = 32);
    null == c && (c = 0);
    null == b && (b = 0);
    this.data = a;
    this.width = b;
    this.height = c;
    this.bitsPerPixel = d;
    this.format = null == f ? 0 : f;
    this.premultiplied = !1;
    this.transparent = !0
};
g["lime.graphics.ImageBuffer"] = Ve;
Ve.__name__ = "lime.graphics.ImageBuffer";
Ve.prototype = {
    clone: function() {
        var a = new Ve(this.data, this.width, this.height, this.bitsPerPixel);
        if (null != this.data) {
            var b = this.data.byteLength,
                c = null,
                d = null,
                f = null,
                h = null,
                k = null;
            b = null != b ? new Uint8Array(b) : null != c ? new Uint8Array(c) : null != d ? new Uint8Array(d.__array) : null != f ? new Uint8Array(f) : null != h ? null == k ? new Uint8Array(h, 0) :
                new Uint8Array(h, 0, k) : null;
            a.data = b;
            d = c = b = null;
            f = this.data;
            k = h = null;
            b = null != b ? new Uint8Array(b) : null != c ? new Uint8Array(c) : null != d ? new Uint8Array(d.__array) : null != f ? new Uint8Array(f) : null != h ? null == k ? new Uint8Array(h, 0) : new Uint8Array(h, 0, k) : null;
            a.data.set(b)
        } else null != this.__srcImageData ? (a.__srcCanvas = window.document.createElement("canvas"), a.__srcContext = a.__srcCanvas.getContext("2d"), a.__srcCanvas.width = this.__srcImageData.width, a.__srcCanvas.height = this.__srcImageData.height, a.__srcImageData =
            a.__srcContext.createImageData(this.__srcImageData.width, this.__srcImageData.height), b = new Uint8ClampedArray(this.__srcImageData.data), a.__srcImageData.data.set(b)) : null != this.__srcCanvas ? (a.__srcCanvas = window.document.createElement("canvas"), a.__srcContext = a.__srcCanvas.getContext("2d"), a.__srcCanvas.width = this.__srcCanvas.width, a.__srcCanvas.height = this.__srcCanvas.height, a.__srcContext.drawImage(this.__srcCanvas, 0, 0)) : a.__srcImage = this.__srcImage;
        a.bitsPerPixel = this.bitsPerPixel;
        a.format = this.format;
        a.premultiplied = this.premultiplied;
        a.transparent = this.transparent;
        return a
    },
    get_src: function() {
        return null != this.__srcImage ? this.__srcImage : this.__srcCanvas
    },
    set_src: function(a) {
        a instanceof Image ? this.__srcImage = a : a instanceof HTMLCanvasElement && (this.__srcCanvas = a, this.__srcContext = this.__srcCanvas.getContext("2d"));
        return a
    },
    get_stride: function() {
        return this.width * (this.bitsPerPixel / 8 | 0)
    },
    __class__: Ve,
    __properties__: {
        get_stride: "get_stride",
        set_src: "set_src",
        get_src: "get_src"
    }
};
var Ka = function() {};
g["lime._internal.graphics.ImageCanvasUtil"] = Ka;
Ka.__name__ = "lime._internal.graphics.ImageCanvasUtil";
Ka.colorTransform = function(a, b, c) {
    Ka.convertToData(a);
    bc.colorTransform(a, b, c)
};
Ka.convertToCanvas = function(a, b) {
    null == b && (b = !1);
    var c = a.buffer;
    null != c.__srcImage ? (null == c.__srcCanvas && (Ka.createCanvas(a, c.__srcImage.width, c.__srcImage.height), c.__srcContext.drawImage(c.__srcImage, 0, 0)), c.__srcImage = null) : null == c.__srcCanvas && null != c.data ? (a.set_transparent(!0), Ka.createCanvas(a, c.width, c.height),
        Ka.createImageData(a), c.__srcContext.putImageData(c.__srcImageData, 0, 0)) : a.type == Zc.DATA && null != c.__srcImageData && a.dirty && (c.__srcContext.putImageData(c.__srcImageData, 0, 0), a.dirty = !1);
    b ? (c.data = null, c.__srcImageData = null) : null == c.data && null != c.__srcImageData && (c.data = c.__srcImageData.data);
    a.type = Zc.CANVAS
};
Ka.convertToData = function(a, b) {
    null == b && (b = !1);
    var c = a.buffer;
    null != c.__srcImage && Ka.convertToCanvas(a);
    if (null != c.__srcCanvas && null == c.data) Ka.createImageData(a), a.type == Zc.CANVAS && (a.dirty = !1);
    else if (a.type == Zc.CANVAS && null != c.__srcCanvas && a.dirty) {
        if (null == c.__srcImageData) Ka.createImageData(a);
        else {
            c.__srcImageData = c.__srcContext.getImageData(0, 0, c.width, c.height);
            var d = c.__srcImageData.data.buffer;
            d = null != d ? new Uint8Array(d) : null;
            c.data = d
        }
        a.dirty = !1
    }
    b && (a.buffer.__srcCanvas = null, a.buffer.__srcContext = null);
    a.type = Zc.DATA
};
Ka.copyChannel = function(a, b, c, d, f, h) {
    Ka.convertToData(b);
    Ka.convertToData(a);
    bc.copyChannel(a, b, c, d, f, h)
};
Ka.copyPixels = function(a, b, c, d, f, h, k) {
    null == k && (k = !1);
    null == d || d.x >= a.width || d.y >= a.height || null == c || 1 > c.width || 1 > c.height || (null != f && f.get_transparent() && (null == h && (h = new jg), b = b.clone(), b.copyChannel(f, new Ld(c.x + h.x, c.y + h.y, c.width, c.height), new jg(c.x, c.y), sg.ALPHA, sg.ALPHA)), Ka.convertToCanvas(a, !0), k || a.get_transparent() && b.get_transparent() && a.buffer.__srcContext.clearRect(d.x + a.offsetX, d.y + a.offsetY, c.width + a.offsetX, c.height + a.offsetY), Ka.convertToCanvas(b), null != b.buffer.get_src() && (a.buffer.__srcContext.globalCompositeOperation = "source-over",
        a.buffer.__srcContext.drawImage(b.buffer.get_src(), c.x + b.offsetX | 0, c.y + b.offsetY | 0, c.width | 0, c.height | 0, d.x + a.offsetX | 0, d.y + a.offsetY | 0, c.width | 0, c.height | 0)), a.dirty = !0, a.version++)
};
Ka.createCanvas = function(a, b, c) {
    var d = a.buffer;
    null == d.__srcCanvas && (d.__srcCanvas = window.document.createElement("canvas"), d.__srcCanvas.width = b, d.__srcCanvas.height = c, a.get_transparent() ? d.__srcContext = d.__srcCanvas.getContext("2d") : (a.get_transparent() || d.__srcCanvas.setAttribute("moz-opaque", "true"), d.__srcContext =
        d.__srcCanvas.getContext("2d", {
            alpha: !1
        })))
};
Ka.createImageData = function(a) {
    a = a.buffer;
    if (null == a.__srcImageData) {
        null == a.data ? a.__srcImageData = a.__srcContext.getImageData(0, 0, a.width, a.height) : (a.__srcImageData = a.__srcContext.createImageData(a.width, a.height), a.__srcImageData.data.set(a.data));
        var b = a.__srcImageData.data.buffer;
        b = null != b ? new Uint8Array(b) : null;
        a.data = b
    }
};
Ka.fillRect = function(a, b, c, d) {
    Ka.convertToCanvas(a);
    if (1 == d) {
        d = c >> 16 & 255;
        var f = c >> 8 & 255;
        var h = c & 255;
        c = a.get_transparent() ? c >> 24 &
            255 : 255
    } else d = c >> 24 & 255, f = c >> 16 & 255, h = c >> 8 & 255, c = a.get_transparent() ? c & 255 : 255;
    0 == b.x && 0 == b.y && b.width == a.width && b.height == a.height && a.get_transparent() && 0 == c ? a.buffer.__srcCanvas.width = a.buffer.width : (255 > c && a.buffer.__srcContext.clearRect(b.x + a.offsetX, b.y + a.offsetY, b.width + a.offsetX, b.height + a.offsetY), 0 < c && (a.buffer.__srcContext.fillStyle = "rgba(" + d + ", " + f + ", " + h + ", " + c / 255 + ")", a.buffer.__srcContext.fillRect(b.x + a.offsetX, b.y + a.offsetY, b.width + a.offsetX, b.height + a.offsetY)), a.dirty = !0, a.version++)
};
Ka.getPixel = function(a, b, c, d) {
    Ka.convertToData(a);
    return bc.getPixel(a, b, c, d)
};
Ka.getPixels = function(a, b, c) {
    Ka.convertToData(a);
    return bc.getPixels(a, b, c)
};
Ka.setPixel = function(a, b, c, d, f) {
    Ka.convertToData(a);
    bc.setPixel(a, b, c, d, f)
};
Ka.sync = function(a, b) {
    null != a && (a.type != Zc.CANVAS || null == a.buffer.__srcCanvas && null == a.buffer.data ? a.type == Zc.DATA && Ka.convertToData(a, b) : Ka.convertToCanvas(a, b))
};
var Ld = function(a, b, c, d) {
    null == d && (d = 0);
    null == c && (c = 0);
    null == b && (b = 0);
    null == a && (a = 0);
    this.x = a;
    this.y = b;
    this.width =
        c;
    this.height = d
};
g["lime.math.Rectangle"] = Ld;
Ld.__name__ = "lime.math.Rectangle";
Ld.prototype = {
    intersection: function(a, b) {
        null == b && (b = new Ld);
        var c = this.x < a.x ? a.x : this.x,
            d = this.get_right() > a.get_right() ? a.get_right() : this.get_right();
        if (d <= c) return b.setEmpty(), b;
        var f = this.y < a.y ? a.y : this.y;
        a = this.get_bottom() > a.get_bottom() ? a.get_bottom() : this.get_bottom();
        if (a <= f) return b.setEmpty(), b;
        b.x = c;
        b.y = f;
        b.width = d - c;
        b.height = a - f;
        return b
    },
    offset: function(a, b) {
        this.x += a;
        this.y += b
    },
    setEmpty: function() {
        this.x =
            this.y = this.width = this.height = 0
    },
    setTo: function(a, b, c, d) {
        this.x = a;
        this.y = b;
        this.width = c;
        this.height = d
    },
    __toFlashRectangle: function() {
        return null
    },
    get_bottom: function() {
        return this.y + this.height
    },
    get_right: function() {
        return this.x + this.width
    },
    __class__: Ld,
    __properties__: {
        get_right: "get_right",
        get_bottom: "get_bottom"
    }
};
var ua = function(a, b, c, d, f, h) {
    null == h && (h = 0);
    null == f && (f = 0);
    null == d && (d = 1);
    null == c && (c = 0);
    null == b && (b = 0);
    null == a && (a = 1);
    this.a = a;
    this.b = b;
    this.c = c;
    this.d = d;
    this.tx = f;
    this.ty = h
};
g["openfl.geom.Matrix"] =
    ua;
ua.__name__ = "openfl.geom.Matrix";
ua.prototype = {
    clone: function() {
        return new ua(this.a, this.b, this.c, this.d, this.tx, this.ty)
    },
    concat: function(a) {
        var b = this.a * a.a + this.b * a.c;
        this.b = this.a * a.b + this.b * a.d;
        this.a = b;
        b = this.c * a.a + this.d * a.c;
        this.d = this.c * a.b + this.d * a.d;
        this.c = b;
        b = this.tx * a.a + this.ty * a.c + a.tx;
        this.ty = this.tx * a.b + this.ty * a.d + a.ty;
        this.tx = b
    },
    copyFrom: function(a) {
        this.a = a.a;
        this.b = a.b;
        this.c = a.c;
        this.d = a.d;
        this.tx = a.tx;
        this.ty = a.ty
    },
    equals: function(a) {
        return null != a && this.tx == a.tx && this.ty ==
            a.ty && this.a == a.a && this.b == a.b && this.c == a.c ? this.d == a.d : !1
    },
    identity: function() {
        this.a = 1;
        this.c = this.b = 0;
        this.d = 1;
        this.ty = this.tx = 0
    },
    invert: function() {
        var a = this.a * this.d - this.b * this.c;
        if (0 == a) this.a = this.b = this.c = this.d = 0, this.tx = -this.tx, this.ty = -this.ty;
        else {
            a = 1 / a;
            var b = this.d * a;
            this.d = this.a * a;
            this.a = b;
            this.b *= -a;
            this.c *= -a;
            a = -this.a * this.tx - this.c * this.ty;
            this.ty = -this.b * this.tx - this.d * this.ty;
            this.tx = a
        }
        return this
    },
    scale: function(a, b) {
        this.a *= a;
        this.b *= b;
        this.c *= a;
        this.d *= b;
        this.tx *= a;
        this.ty *=
            b
    },
    setTo: function(a, b, c, d, f, h) {
        this.a = a;
        this.b = b;
        this.c = c;
        this.d = d;
        this.tx = f;
        this.ty = h
    },
    transformPoint: function(a) {
        return new I(a.x * this.a + a.y * this.c + this.tx, a.x * this.b + a.y * this.d + this.ty)
    },
    translate: function(a, b) {
        this.tx += a;
        this.ty += b
    },
    __class__: ua
};
var Tb = function(a, b, c, d, f, h, k, n) {
    null == n && (n = 0);
    null == k && (k = 0);
    null == h && (h = 0);
    null == f && (f = 0);
    null == d && (d = 1);
    null == c && (c = 1);
    null == b && (b = 1);
    null == a && (a = 1);
    this.redMultiplier = a;
    this.greenMultiplier = b;
    this.blueMultiplier = c;
    this.alphaMultiplier = d;
    this.redOffset =
        f;
    this.greenOffset = h;
    this.blueOffset = k;
    this.alphaOffset = n
};
g["openfl.geom.ColorTransform"] = Tb;
Tb.__name__ = "openfl.geom.ColorTransform";
Tb.prototype = {
    __clone: function() {
        return new Tb(this.redMultiplier, this.greenMultiplier, this.blueMultiplier, this.alphaMultiplier, this.redOffset, this.greenOffset, this.blueOffset, this.alphaOffset)
    },
    __copyFrom: function(a) {
        this.redMultiplier = a.redMultiplier;
        this.greenMultiplier = a.greenMultiplier;
        this.blueMultiplier = a.blueMultiplier;
        this.alphaMultiplier = a.alphaMultiplier;
        this.redOffset = a.redOffset;
        this.greenOffset = a.greenOffset;
        this.blueOffset = a.blueOffset;
        this.alphaOffset = a.alphaOffset
    },
    __combine: function(a) {
        this.redMultiplier *= a.redMultiplier;
        this.greenMultiplier *= a.greenMultiplier;
        this.blueMultiplier *= a.blueMultiplier;
        this.alphaMultiplier *= a.alphaMultiplier;
        this.redOffset += a.redOffset;
        this.greenOffset += a.greenOffset;
        this.blueOffset += a.blueOffset;
        this.alphaOffset += a.alphaOffset
    },
    __identity: function() {
        this.alphaMultiplier = this.blueMultiplier = this.greenMultiplier =
            this.redMultiplier = 1;
        this.alphaOffset = this.blueOffset = this.greenOffset = this.redOffset = 0
    },
    __invert: function() {
        this.redMultiplier = 0 != this.redMultiplier ? 1 / this.redMultiplier : 1;
        this.greenMultiplier = 0 != this.greenMultiplier ? 1 / this.greenMultiplier : 1;
        this.blueMultiplier = 0 != this.blueMultiplier ? 1 / this.blueMultiplier : 1;
        this.alphaMultiplier = 0 != this.alphaMultiplier ? 1 / this.alphaMultiplier : 1;
        this.redOffset = -this.redOffset;
        this.greenOffset = -this.greenOffset;
        this.blueOffset = -this.blueOffset;
        this.alphaOffset = -this.alphaOffset
    },
    __equals: function(a, b) {
        return null == a || this.redMultiplier != a.redMultiplier || this.greenMultiplier != a.greenMultiplier || this.blueMultiplier != a.blueMultiplier || !b && this.alphaMultiplier != a.alphaMultiplier || this.redOffset != a.redOffset || this.greenOffset != a.greenOffset || this.blueOffset != a.blueOffset ? !1 : this.alphaOffset == a.alphaOffset
    },
    __isDefault: function(a) {
        return a ? 1 == this.redMultiplier && 1 == this.greenMultiplier && 1 == this.blueMultiplier && 0 == this.redOffset && 0 == this.greenOffset && 0 == this.blueOffset ? 0 == this.alphaOffset :
            !1 : 1 == this.redMultiplier && 1 == this.greenMultiplier && 1 == this.blueMultiplier && 1 == this.alphaMultiplier && 0 == this.redOffset && 0 == this.greenOffset && 0 == this.blueOffset ? 0 == this.alphaOffset : !1
    },
    __setArrays: function(a, b) {
        a[0] = this.redMultiplier;
        a[1] = this.greenMultiplier;
        a[2] = this.blueMultiplier;
        a[3] = this.alphaMultiplier;
        b[0] = this.redOffset;
        b[1] = this.greenOffset;
        b[2] = this.blueOffset;
        b[3] = this.alphaOffset
    },
    get_color: function() {
        return (this.redOffset | 0) << 16 | (this.greenOffset | 0) << 8 | this.blueOffset | 0
    },
    set_color: function(a) {
        this.redOffset =
            a >> 16 & 255;
        this.greenOffset = a >> 8 & 255;
        this.blueOffset = a & 255;
        this.blueMultiplier = this.greenMultiplier = this.redMultiplier = 0;
        return this.get_color()
    },
    __toLimeColorMatrix: function() {
        null == Tb.__limeColorMatrix && (Tb.__limeColorMatrix = new Float32Array(20));
        Tb.__limeColorMatrix[0] = this.redMultiplier;
        Tb.__limeColorMatrix[4] = this.redOffset / 255;
        Tb.__limeColorMatrix[6] = this.greenMultiplier;
        Tb.__limeColorMatrix[9] = this.greenOffset / 255;
        Tb.__limeColorMatrix[12] = this.blueMultiplier;
        Tb.__limeColorMatrix[14] = this.blueOffset /
            255;
        Tb.__limeColorMatrix[18] = this.alphaMultiplier;
        Tb.__limeColorMatrix[19] = this.alphaOffset / 255;
        return Tb.__limeColorMatrix
    },
    __class__: Tb,
    __properties__: {
        set_color: "set_color",
        get_color: "get_color"
    }
};
var jd = function() {
    this.keyMap = new mc;
    ia.call(this);
    this.brush = new md;
    this.brush.set_cacheAsBitmap(!0);
    this.addChild(this.brush);
    this.btnMenu = new fb("Menu");
    this.btnMenu.click.add(l(this, this.onMenu));
    this.addChild(this.btnMenu);
    this.nodePatches = new pa;
    this.prevState = new pa;
    for (var a = 0, b = this.model.cells; a <
        b.length;) {
        var c = b[a];
        ++a;
        for (var d = 0, f = c.shape; d < f.length;) {
            var h = f[d];
            ++d;
            if (null == this.nodePatches.h.__keys__[h.__id__]) {
                this.nodePatches.set(h, [c]);
                var k = this.prevState,
                    n = h.clone();
                k.set(h, n)
            } else this.nodePatches.h[h.__id__].push(c)
        }
    }
    k = this.keyMap;
    h = new Kf(this);
    k.h[68] = h;
    k = this.keyMap;
    h = new ui(this);
    k.h[82] = h;
    k = this.keyMap;
    h = new Lf(this);
    k.h[76] = h;
    k = this.keyMap;
    h = new Mf(this);
    k.h[88] = h;
    k = this.keyMap;
    h = new me(this);
    k.h[66] = h;
    k = this.keyMap;
    h = new vi(this);
    k.h[80] = h;
    k = this.keyMap;
    h = new wi(this);
    k.h[77] = h;
    k = this.keyMap;
    h = new xi(this);
    k.h[69] = h;
    if (null == jd.lastTool) this.switchTool(this.keyMap.h[68]);
    else
        for (a = this.keyMap.iterator(); a.hasNext();)
            if (b = a.next(), va.getClass(b) == va.getClass(jd.lastTool)) {
                this.switchTool(b);
                break
            } this.mesh = new ka
};
g["com.watabou.mfcg.scenes.WarpScene"] = jd;
jd.__name__ = "com.watabou.mfcg.scenes.WarpScene";
jd.__super__ = ia;
jd.prototype = v(ia.prototype, {
    onEsc: function() {
        this.onDiscard()
    },
    onKeyEvent: function(a, b) {
        if (!b || !this.tool.onKey(a))
            if (13 == a) {
                if (b) this.onSave()
            } else b &&
                this.keyMap.h.hasOwnProperty(a) ? this.switchTool(this.keyMap.h[a]) : ia.prototype.onKeyEvent.call(this, a, b)
    },
    switchTool: function(a) {
        jd.lastTool = this.tool = a;
        var b = ia.map;
        null != b && (a.activate(), a.onMove(b.get_mouseX(), b.get_mouseY()), q.show(a.getName()))
    },
    layout: function() {
        ia.prototype.layout.call(this);
        this.brush.set_x(this.get_mouseX());
        this.brush.set_y(this.get_mouseY());
        this.tool.activate();
        this.btnMenu.set_x(this.rWidth - this.btnMenu.get_width() - 2);
        this.btnMenu.set_y(2)
    },
    recreateMap: function() {
        ia.prototype.recreateMap.call(this);
        ia.map.addChild(this.mesh);
        ia.map.mouseChildren = !1
    },
    createOverlays: function() {
        ia.prototype.createOverlays.call(this);
        for (var a = 0, b = this.overlays; a < b.length;) {
            var c = b[a];
            ++a;
            c.mouseEnabled = !1;
            c.mouseChildren = !1
        }
    },
    activate: function() {
        ia.prototype.activate.call(this);
        for (var a = [], b = 0, c = Ec.tools; b < c.length;) {
            var d = c[b];
            ++b;
            d = u.findWidnow(d);
            null != d && a.push(d.content)
        }
        b = a;
        u.wipe();
        for (a = 0; a < b.length;) c = b[a], ++a, c.forceDisplay();
        this.stage.addEventListener("mouseWheel", l(this, this.onMouseWheel));
        this.stage.addEventListener("mouseDown",
            l(this, this.onMouseDown));
        this.stage.addEventListener("mouseUp", l(this, this.onMouseUp));
        this.stage.addEventListener("mouseMove", l(this, this.onMouseMove));
        this.brush.set_x(this.get_mouseX());
        this.brush.set_y(this.get_mouseY());
        q.show(this.tool.getName())
    },
    deactivate: function() {
        this.stage.removeEventListener("mouseWheel", l(this, this.onMouseWheel));
        this.stage.removeEventListener("mouseDown", l(this, this.onMouseDown));
        this.stage.removeEventListener("mouseUp", l(this, this.onMouseUp));
        this.stage.removeEventListener("mouseMove",
            l(this, this.onMouseMove));
        ia.prototype.deactivate.call(this)
    },
    updateBrush: function(a, b) {
        null == b && (b = .5);
        this.brush.get_graphics().clear();
        0 < a && (this.brush.get_graphics().lineStyle(1, 52224), this.brush.get_graphics().drawCircle(0, 0, a * ia.map.get_scaleX()), this.brush.get_graphics().lineStyle(2, 52224), this.brush.get_graphics().drawCircle(0, 0, a * ia.map.get_scaleX() * b))
    },
    onMouseWheel: function(a) {
        this.tool.onWheel(ia.map.get_mouseX(), ia.map.get_mouseY(), a.delta)
    },
    onMouseMove: function(a) {
        this.brush.set_x(this.get_mouseX());
        this.brush.set_y(this.get_mouseY());
        a = ia.map.get_mouseX();
        var b = ia.map.get_mouseY();
        if (this.down) this.tool.onDrag(a, b);
        else this.tool.onMove(a, b)
    },
    onMouseDown: function(a) {
        this.down = !0;
        this.tool.onPress(ia.map.get_mouseX(), ia.map.get_mouseY())
    },
    onMouseUp: function(a) {
        this.tool.onRelease();
        this.down = !1
    },
    onMenu: function() {
        this.onContext(ia.getMenu());
        this.showMenu(this.btnMenu)
    },
    onContext: function(a) {
        for (var b = this, c = this.keyMap.iterator(); c.hasNext();) {
            var d = [c.next()];
            a.addItem(d[0].getName(), function(a) {
                    return function() {
                        b.switchTool(a[0])
                    }
                }(d),
                d[0] == this.tool)
        }
        a.addSeparator();
        a.addItem("Apply", l(this, this.onSave));
        a.addItem("Discard", l(this, this.onDiscard))
    },
    submit: function(a, b) {
        this.model.updateGeometry(a);
        b && (this.model.ocean = null);
        this.drawMap();
        this.layoutLabels();
        this.model.updateLandmarks();
        this.markers.setSize(this.rWidth, this.rHeight)
    },
    clearMesh: function() {
        for (var a = 0, b = this.mesh.get_numChildren(); a < b;) {
            var c = a++;
            jd.cache.push(this.mesh.getChildAt(c))
        }
        this.mesh.removeChildren()
    },
    drawEdge: function(a, b, c) {
        var d = 0 < jd.cache.length ?
            jd.cache.pop() : new Nd(jd.pixel);
        d.set_x(a.x);
        d.set_y(a.y);
        d.set_rotation(Math.atan2(b.y - a.y, b.x - a.x) / Math.PI * 180);
        d.set_scaleX(I.distance(a, b));
        .5 <= c ? (d.set_scaleY(c * K.lineInvScale), d.set_alpha(1)) : (d.set_scaleY(.5 * K.lineInvScale), d.set_alpha(c / .5));
        this.mesh.addChild(d)
    },
    drawNode: function(a, b) {
        b *= K.lineInvScale;
        var c = 0 < jd.cache.length ? jd.cache.pop() : new Nd(jd.pixel);
        c.set_alpha(1);
        c.set_rotation(0);
        c.set_scaleX(c.set_scaleY(2 * b));
        c.set_x(a.x - b);
        c.set_y(a.y - b);
        this.mesh.addChild(c)
    },
    onSave: function() {
        this.model.updateDimensions();
        bb.switchScene(Ec)
    },
    onDiscard: function() {
        for (var a = this.prevState.keys(); a.hasNext();) {
            var b = a.next(),
                c = this.prevState.h[b.__id__];
            b.setTo(c.x, c.y)
        }
        this.model.updateGeometry(this.model.cells);
        this.model.ocean = null;
        bb.switchScene(Ec)
    },
    __class__: jd
});
var tb = function(a) {
    ka.call(this);
    0 == tb.convexity && this.reroll();
    this.radius = a;
    this.update();
    this.label = new ka;
    this.addChild(this.label);
    a = vb.getFormat("font_element", vb.fontElement, K.colorDark, 1.2857142857142858);
    this.tf = vb.get("N", a);
    this.tf.mouseEnabled = !1;
    this.label.addChild(this.tf);
    this.addEventListener("click", l(this, this.reset));
    this.addEventListener("mouseWheel", l(this, this.rotate))
};
g["com.watabou.mfcg.scenes.overlays.Compass"] = tb;
tb.__name__ = "com.watabou.mfcg.scenes.overlays.Compass";
tb.__super__ = ka;
tb.prototype = v(ka.prototype, {
    reroll: function() {
        tb.convexity = .1 + .2 * ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647);
        var a = .9;
        null == a && (a = .5);
        tb.secondary = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a ? tb.convexity + (1 - tb.convexity) * (.1 + .9 * ((C.seed =
            48271 * C.seed % 2147483647 | 0) / 2147483647)) : 0;
        a = .8;
        null == a && (a = .5);
        (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a ? (tb.mainRing = tb.convexity + (1 - tb.convexity) * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3), tb.auxRing = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) : tb.mainRing = tb.auxRing = 0;
        a = Math.pow(1 - tb.convexity, 2);
        null == a && (a = .5);
        (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a ? (tb.north = 1.3 + .6 * (((C.seed =
            48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3), a = .7, null == a && (a = .5), tb.south = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a ? 1 + (tb.north - 1) * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3) : 1) : tb.north = tb.south = 1
    },
    update: function() {
        var a = this.get_graphics();
        a.clear();
        a.beginFill(16711680, 0);
        a.drawCircle(0, 0, this.radius);
        a.endFill();
        if (0 < tb.mainRing) {
            var b = !1;
            null == b && (b = !0);
            a.lineStyle(K.getStrokeWidth(K.strokeThick, b), K.colorDark);
            a.drawCircle(0, 0, this.radius * tb.mainRing);
            b = !1;
            null == b && (b = !0);
            a.lineStyle(K.getStrokeWidth(K.strokeNormal, b), K.colorDark);
            a.drawCircle(0, 0, this.radius * tb.auxRing);
            a.endFill()
        }
        this.drawStar(tb.secondary, tb.convexity * tb.secondary, .5);
        this.drawStar(1, tb.convexity, 0)
    },
    drawStar: function(a, b, c) {
        var d = this.get_graphics(),
            f = [];
        f.push(I.polar(this.radius * a, Math.PI / 2 * c));
        f.push(I.polar(this.radius *
            b, Math.PI / 2 * (c + .5)));
        f.push(I.polar(this.radius * a, Math.PI / 2 * (1 + c)));
        f.push(I.polar(this.radius * b, Math.PI / 2 * (1 + c + .5)));
        f.push(I.polar(this.radius * a, Math.PI / 2 * (2 + c)));
        f.push(I.polar(this.radius * b, Math.PI / 2 * (2 + c + .5)));
        f.push(I.polar(this.radius * a, Math.PI / 2 * (3 + c)));
        f.push(I.polar(this.radius * b, Math.PI / 2 * (3 + c + .5)));
        d.beginFill(K.colorDark);
        Kb.drawPolygon(d, [f[0], f[1], tb.o]);
        Kb.drawPolygon(d, [f[2], f[3], tb.o]);
        Kb.drawPolygon(d, [f[4], f[5], tb.o]);
        Kb.drawPolygon(d, [f[6], f[7], tb.o]);
        d.beginFill(K.colorLight);
        Kb.drawPolygon(d, [f[1], f[2], tb.o]);
        Kb.drawPolygon(d, [f[3], f[4], tb.o]);
        Kb.drawPolygon(d, [f[5], f[6], tb.o]);
        Kb.drawPolygon(d, [f[7], f[0], tb.o]);
        d.endFill();
        a = !1;
        null == a && (a = !0);
        d.lineStyle(K.getStrokeWidth(K.strokeNormal, a), K.colorDark);
        Kb.drawPolygon(d, f);
        d.endFill()
    },
    updateLabel: function(a) {
        this.tf.set_text(["N", "W", "S", "E"][a]);
        this.tf.set_x(-this.tf.get_width() / 2);
        this.tf.set_y(-this.tf.get_height() + 2);
        this.label.set_rotation(-90 * a);
        a = I.polar(this.radius, -(a + 1) * Math.PI / 2);
        this.label.set_x(a.x);
        this.label.set_y(a.y)
    },
    reset: function(a) {
        a.ctrlKey || a.commandKey ? this.updateNorth(Ub.instance.north = 0) : a.shiftKey && (this.reroll(), this.update())
    },
    rotate: function(a) {
        var b = this.get_rotation();
        a = a.delta;
        this.set_rotation(b + 10 * (0 == a ? 0 : 0 > a ? -1 : 1));
        this.updateLabel((this.get_rotation() + 405) % 360 / 90 | 0);
        Ub.instance.north = this.get_rotation()
    },
    updateNorth: function(a) {
        this.set_rotation(a);
        this.updateLabel((this.get_rotation() + 405) % 360 / 90 | 0)
    },
    __class__: tb
});
var Ca = function(a) {
    var b = this;
    U.call(this);
    this.scene = a;
    this.addEventListener("removedFromStage",
        function(a) {
            b.onDestroy()
        });
    this.addEventListener("rightClick", function(a) {
        b.onContext(ia.getMenu())
    });
    Bb.newModel.add(l(this, this.onNewModel))
};
g["com.watabou.mfcg.scenes.overlays.Overlay"] = Ca;
Ca.__name__ = "com.watabou.mfcg.scenes.overlays.Overlay";
Ca.__super__ = U;
Ca.prototype = v(U.prototype, {
    map2layer: function(a, b) {
        return this.globalToLocal(a.localToGlobal(b))
    },
    layer2map: function(a, b) {
        return a.globalToLocal(this.localToGlobal(b))
    },
    update: function(a) {
        this.model = a
    },
    exportPNG: function(a) {},
    onContext: function(a) {},
    onNewModel: function(a) {
        this.model = a
    },
    onDestroy: function() {
        Bb.newModel.remove(l(this, this.onNewModel))
    },
    __class__: Ca
});
var qg = function(a) {
    this.pos = yc.BOTTOM_LEFT;
    this.compass = new tb(qg.RADIUS);
    Ca.call(this, a);
    this.addChild(this.compass)
};
g["com.watabou.mfcg.scenes.overlays.CompassOverlay"] = qg;
qg.__name__ = "com.watabou.mfcg.scenes.overlays.CompassOverlay";
qg.__super__ = Ca;
qg.prototype = v(Ca.prototype, {
    update: function(a) {
        Ca.prototype.update.call(this, a);
        this.compass.updateNorth(a.north)
    },
    layout: function() {
        switch (this.pos._hx_index) {
            case 1:
                this.compass.set_x(this.compass.radius +
                    Ca.MARGIN);
                this.compass.set_y(this.compass.radius + Ca.MARGIN);
                break;
            case 2:
                this.compass.set_x(this.rWidth - this.compass.radius - Ca.MARGIN);
                this.compass.set_y(this.compass.radius + Ca.MARGIN);
                break;
            case 3:
                this.compass.set_x(this.compass.radius + Ca.MARGIN);
                this.compass.set_y(this.rHeight - this.compass.radius - Ca.MARGIN);
                break;
            case 4:
                this.compass.set_x(this.rWidth - this.compass.radius - Ca.MARGIN), this.compass.set_y(this.rHeight - this.compass.radius - Ca.MARGIN)
        }
    },
    set_position: function(a) {
        this.pos != a && (this.pos = a,
            this.layout());
        return a
    },
    onContext: function(a) {
        var b = this;
        a.addItem("Reroll", function() {
            b.compass.reroll();
            b.compass.update()
        });
        a.addItem("Reset", function() {
            b.compass.updateNorth(b.model.north = 0)
        });
        a.addItem("Hide", function() {
            ba.set("compass", b.set_visible(!1))
        })
    },
    __class__: qg
});
var tg = function() {
    ka.call(this);
    this.mouseEnabled = !0;
    this.mouseChildren = !1
};
g["com.watabou.mfcg.scenes.overlays.CurvedLabel"] = tg;
tg.__name__ = "com.watabou.mfcg.scenes.overlays.CurvedLabel";
tg.getRidgeSegment = function(a, b) {
    var c =
        new yi(Hf.render(a, !1, 0)),
        d = c.length(),
        f = .8 * d;
    b = (d - (f > b ? .5 * (f + b) : f)) / 2;
    c = c.getSegment(b, d - b);
    uc.visvalingam(c, 3, 1.2);
    c.unshift(a[0]);
    c.push(a[a.length - 1]);
    a = Me.smoothOpen(c, 4);
    a = a.slice(4, -4);
    a[0].x > a[a.length - 1].x && a.reverse();
    return a
};
tg.__super__ = ka;
tg.prototype = v(ka.prototype, {
    setText: function(a) {
        this.removeChildren();
        var b = vb.getFormat("font_label", vb.fontLabel, K.colorLabel),
            c = vb.get(a, b);
        this.length = c.get_textWidth();
        this.len = a.length;
        this.pos = [];
        this.letters = [];
        for (var d = 0, f = this.len; d < f;) {
            var h =
                d++,
                k = c.getCharBoundaries(h);
            this.pos.push(k.get_left() + k.width / 2);
            h = new zi(a.charAt(h), b);
            this.letters.push(h);
            this.addChild(h)
        }
    },
    arrange: function(a) {
        a = tg.getRidgeSegment(a, this.length);
        var b = new yi(a),
            c = b.length(),
            d = c / this.length,
            f = 0;
        1 < d && (d = Math.min(Math.sqrt(d), 1.5), 1 < this.len && (f = (c - this.length * d) / (this.len - 1)));
        c = (c - (this.length * d + f * (this.len - 1))) / 2;
        for (var h = 0, k = this.len; h < k;) {
            var n = h++,
                p = b.getPos(c + this.pos[n] * d + n * f);
            p = null != p ? p : a[a.length - 1];
            n = this.letters[n];
            var g = b.get_tangent();
            n.set_rotation(Math.atan2(g.y,
                g.x) / Math.PI * 180);
            n.set_scaleX(n.set_scaleY(d));
            n.set_x(p.x);
            n.set_y(p.y)
        }
    },
    center: function() {
        var a = this.getBounds(this.parent);
        return new I(a.get_left() + a.width / 2, a.get_top() + a.height / 2)
    },
    addOutline: function() {
        this.removeOutline();
        var a = !1;
        null == a && (a = !0);
        a = K.getStrokeWidth(K.strokeThick, a);
        this.outline = new jh(this, K.colorPaper, a, 8);
        this.addChildAt(this.outline, 0)
    },
    removeOutline: function() {
        null != this.outline && (this.removeChild(this.outline), this.outline = null)
    },
    __class__: tg
});
var zi = function(a, b) {
    ka.call(this);
    a = vb.get(a, b);
    a.set_x(-a.get_width() / 2);
    a.set_y(-a.get_height() / 2);
    this.addChild(a)
};
g["com.watabou.mfcg.scenes.overlays._CurvedLabel.Letter"] = zi;
zi.__name__ = "com.watabou.mfcg.scenes.overlays._CurvedLabel.Letter";
zi.__super__ = ka;
zi.prototype = v(ka.prototype, {
    __class__: zi
});
var ra = function() {
    this.ready = new Nc;
    var a = this;
    U.call(this);
    this.bmp = new Nd(null, null, !0);
    this.addChild(this.bmp);
    ra.seed = ba.get("emblem_seed", 1);
    ra.coa = ba.get("emblem_coa");
    ra.updated.add(l(this, this.onUpdated));
    ra.setHiRes.add(l(this,
        this.onResolution));
    this.addEventListener("removed", function(b) {
        ra.updated.remove(l(a, a.onUpdated));
        ra.setHiRes.remove(l(a, a.onResolution))
    });
    if (!ra.loading)
        if (null == ra.loRes) ra.loadLo();
        else this.onUpdated()
};
g["com.watabou.mfcg.scenes.overlays.Emblem"] = ra;
ra.__name__ = "com.watabou.mfcg.scenes.overlays.Emblem";
ra.loadLo = function() {
    ra.loading = !0;
    Fb.loadFromFile(null == ra.coa ? "" + ra.ARMORIA + "/png/" + ra.LO_RES + "/" + ra.seed : "" + ra.ARMORIA + "/?format=png&size=" + ra.LO_RES + "&coa=" + ra.coa).onComplete(ra.onLoadLo).onError(ra.onError)
};
ra.onError = function(a) {
    ra.loading = !1;
    ra.setCOA(null)
};
ra.onLoadLo = function(a) {
    ra.loading = !1;
    null != ra.loRes && ra.loRes.dispose();
    ra.loRes = a;
    null == ra.hiRes && ra.loadHi();
    null == ra.svg && ra.loadSvg();
    ra.updated.dispatch()
};
ra.loadHi = function() {
    Fb.loadFromFile(null == ra.coa ? "" + ra.ARMORIA + "/png/" + ra.HI_RES + "/" + ra.seed : "" + ra.ARMORIA + "/?format=png&size=" + ra.HI_RES + "&coa=" + ra.coa).onComplete(ra.onLoadHi)
};
ra.onLoadHi = function(a) {
    null != ra.hiRes && ra.hiRes.dispose();
    ra.hiRes = a
};
ra.loadSvg = function() {
    var a = null ==
        ra.coa ? "" + ra.ARMORIA + "/svg/" + ra.LO_RES + "/" + ra.seed : "" + ra.ARMORIA + "/?format=svg&size=" + ra.LO_RES + "&coa=" + ra.coa,
        b = new Ai;
    b.addEventListener("complete", function(a) {
        ra.onLoadSvg(b.data)
    });
    b.addEventListener("ioError", ra.onIOError);
    b.load(new If(a))
};
ra.onLoadSvg = function(a) {
    ra.svg = W.parse(a).firstElement()
};
ra.onIOError = function(a) {
    if (0 == a.errorID) ra.onLoadSvg(a.text)
};
ra.setCOA = function(a) {
    ba.set("emblem_coa", ra.coa = a);
    ra.loRes = ra.hiRes = null;
    ra.svg = null;
    ra.loadLo()
};
ra.setResoltion = function(a) {
    ra.setHiRes.dispatch(a)
};
ra.__super__ = U;
ra.prototype = v(U.prototype, {
    customize: function() {
        null == u.findWidnow(Nf) && u.showDialog(new Nf(this))
    },
    onUpdated: function() {
        this.bmp.set_bitmapData(ra.loRes);
        this.rWidth = this.bmp.get_width();
        this.rHeight = this.bmp.get_height();
        this.ready.dispatch()
    },
    reroll: function() {
        ba.set("emblem_seed", ra.seed = (new Date).getTime() % 2147483647 | 0);
        ba.set("emblem_coa", ra.coa = null);
        ra.loRes = ra.hiRes = null;
        ra.svg = null;
        ra.loadLo()
    },
    onResolution: function(a) {
        a && null != ra.hiRes ? (this.bmp.set_bitmapData(ra.hiRes),
            this.bmp.set_scaleX(this.bmp.set_scaleY(ra.LO_RES / ra.HI_RES)), this.bmp.smoothing = !0) : (this.bmp.set_bitmapData(ra.loRes), this.bmp.set_scaleX(this.bmp.set_scaleY(1)), a && q.show("High resolution emblem is not loaded"))
    },
    __class__: ra
});
var yc = y["com.watabou.mfcg.scenes.overlays.Position"] = {
    __ename__: "com.watabou.mfcg.scenes.overlays.Position",
    __constructs__: null,
    UNDEFINED: {
        _hx_name: "UNDEFINED",
        _hx_index: 0,
        __enum__: "com.watabou.mfcg.scenes.overlays.Position",
        toString: r
    },
    TOP_LEFT: {
        _hx_name: "TOP_LEFT",
        _hx_index: 1,
        __enum__: "com.watabou.mfcg.scenes.overlays.Position",
        toString: r
    },
    TOP_RIGHT: {
        _hx_name: "TOP_RIGHT",
        _hx_index: 2,
        __enum__: "com.watabou.mfcg.scenes.overlays.Position",
        toString: r
    },
    BOTTOM_LEFT: {
        _hx_name: "BOTTOM_LEFT",
        _hx_index: 3,
        __enum__: "com.watabou.mfcg.scenes.overlays.Position",
        toString: r
    },
    BOTTOM_RIGHT: {
        _hx_name: "BOTTOM_RIGHT",
        _hx_index: 4,
        __enum__: "com.watabou.mfcg.scenes.overlays.Position",
        toString: r
    }
};
yc.__constructs__ = [yc.UNDEFINED, yc.TOP_LEFT, yc.TOP_RIGHT, yc.BOTTOM_LEFT, yc.BOTTOM_RIGHT];
var nf = function(a) {
    this.emblem =
        new ra;
    this.emblem.ready.add(l(this, this.layout));
    Ca.call(this, a);
    this.addChild(this.emblem)
};
g["com.watabou.mfcg.scenes.overlays.EmblemOverlay"] = nf;
nf.__name__ = "com.watabou.mfcg.scenes.overlays.EmblemOverlay";
nf.__super__ = Ca;
nf.prototype = v(Ca.prototype, {
    layout: function() {
        switch (nf.pos._hx_index) {
            case 1:
                this.emblem.set_x(Ca.MARGIN);
                this.emblem.set_y(Ca.MARGIN);
                break;
            case 2:
                this.emblem.set_x(this.rWidth - this.emblem.get_width() - Ca.MARGIN);
                this.emblem.set_y(Ca.MARGIN);
                break;
            case 3:
                this.emblem.set_x(Ca.MARGIN);
                this.emblem.set_y(this.rHeight - this.emblem.get_height() - Ca.MARGIN);
                break;
            case 4:
                this.emblem.set_x(this.rWidth - this.emblem.get_width() - Ca.MARGIN), this.emblem.set_y(this.rHeight - this.emblem.get_height() - Ca.MARGIN)
        }
    },
    set_position: function(a) {
        nf.pos != a && (nf.pos = a, this.layout());
        return a
    },
    onContext: function(a) {
        a.addItem("Customize...", (G = this.emblem, l(G, G.customize)));
        a.addItem("Reroll", (G = this.emblem, l(G, G.reroll)));
        a.addItem("Hide", function() {
            ba.set("emblem", !1)
        })
    },
    exportPNG: function(a) {
        ra.setResoltion(a)
    },
    __class__: nf
});
var Bi = function() {
    this.padding = 10;
    U.call(this)
};
g["com.watabou.mfcg.scenes.overlays.Frame"] = Bi;
Bi.__name__ = "com.watabou.mfcg.scenes.overlays.Frame";
Bi.__super__ = U;
Bi.prototype = v(U.prototype, {
    layout: function() {
        var a = K.colorDark,
            b = this.get_graphics();
        b.clear();
        var c = !1;
        null == c && (c = !0);
        var d = 2 * K.getStrokeWidth(K.strokeThick, c);
        c = !1;
        null == c && (c = !0);
        b.lineStyle(K.getStrokeWidth(K.strokeThick, c), a, null, null, null, null, 1);
        b.drawRect(0, 0, this.rWidth, this.rHeight);
        c = !1;
        null == c && (c = !0);
        b.lineStyle(K.getStrokeWidth(K.strokeNormal,
            c), a, null, null, null, null, 1);
        b.drawRect(d, d, this.rWidth - 2 * d, this.rHeight - 2 * d)
    },
    setInner: function(a, b) {
        this.setSize(Math.ceil(a + 2 * this.padding), Math.ceil(b + 2 * this.padding))
    },
    outline: function(a) {
        this.setInner(a.get_width(), a.get_height());
        a.set_x(this.get_x() + this.padding);
        a.set_y(this.get_y() + this.padding)
    },
    __class__: Bi
});
var ki = function(a) {
    Ca.call(this, a);
    Bb.unitsChanged.add(l(this, this.layout))
};
g["com.watabou.mfcg.scenes.overlays.GridOverlay"] = ki;
ki.__name__ = "com.watabou.mfcg.scenes.overlays.GridOverlay";
ki.__super__ = Ca;
ki.prototype = v(Ca.prototype, {
    onDestroy: function() {
        Ca.prototype.onDestroy.call(this);
        Bb.unitsChanged.remove(l(this, this.layout))
    },
    layout: function() {
        var a = this.get_graphics();
        a.clear();
        var b = !1;
        null == b && (b = !0);
        a.lineStyle(K.getStrokeWidth(K.strokeThin, b), K.colorDark);
        b = ia.map;
        for (var c = Db.get_current().getPlank(b, 100), d = this.layer2map(b, new I), f = this.layer2map(b, new I(this.rWidth, this.rHeight)), h = new I, k = Math.ceil(d.x / c), n = Math.ceil(f.x / c); k < n;) {
            var p = k++;
            h.setTo(p * c, 0);
            h = this.map2layer(b,
                h);
            a.moveTo(h.x, 0);
            a.lineTo(h.x, this.rHeight)
        }
        k = Math.ceil(d.y / c);
        for (n = Math.ceil(f.y / c); k < n;) p = k++, h.setTo(0, p * c), h = this.map2layer(b, h), a.moveTo(0, h.y), a.lineTo(this.rWidth, h.y)
    },
    __class__: ki
});
var oi = function(a) {
    Ca.call(this, a);
    Bb.districtsChanged.add(l(this, this.updateAll))
};
g["com.watabou.mfcg.scenes.overlays.LabelsOverlay"] = oi;
oi.__name__ = "com.watabou.mfcg.scenes.overlays.LabelsOverlay";
oi.__super__ = Ca;
oi.prototype = v(Ca.prototype, {
    onDestroy: function() {
        Ca.prototype.onDestroy.call(this);
        Bb.districtsChanged.remove(l(this,
            this.updateAll))
    },
    update: function(a) {
        Ca.prototype.update.call(this, a);
        this.recreateLabels()
    },
    layout: function() {
        for (var a = this.labels, b = a.keys(); b.hasNext();) {
            var c = b.next(),
                d = a.get(c);
            this.layoutDistrict(c, d)
        }
    },
    recreateLabels: function() {
        var a = this;
        this.removeChildren();
        for (var b = this.model.focus, c = new pa, d = 0, f = this.model.districts; d < f.length;) {
            var h = [f[d]];
            ++d;
            if (null == b || Z.intersects(h[0].faces, b.faces)) {
                var k = [new tg];
                this.updateDistrict(h[0], k[0]);
                this.addChild(k[0]);
                k[0].addEventListener("mouseDown",
                    function(b, c) {
                        return function(d) {
                            a.edit(c[0], b[0])
                        }
                    }(k, h));
                k[0].addEventListener("rightClick", function(b, c) {
                    return function(d) {
                        a.context(c[0], b[0])
                    }
                }(k, h));
                c.set(h[0], k[0])
            }
        }
        this.labels = c;
        this.exportPNG(!1)
    },
    updateDistrict: function(a, b) {
        b.setText(a.name.toUpperCase())
    },
    layoutDistrict: function(a, b) {
        a = a.getRidge();
        for (var c = [], d = 0; d < a.length;) {
            var f = a[d];
            ++d;
            c.push(this.map2layer(ia.map, f))
        }
        b.arrange(c)
    },
    updateAll: function() {
        for (var a = this.labels, b = a.keys(); b.hasNext();) {
            var c = b.next(),
                d = a.get(c);
            this.updateDistrict(c,
                d)
        }
        this.layout()
    },
    edit: function(a, b) {
        var c = this;
        b.set_visible(!1);
        var d = vb.getFormat("font_label", vb.fontLabel, K.colorLabel),
            f = b.center();
        new We(a.name, d, f, this.stage, null, function(d) {
            a.name = "" != d ? d : "-";
            c.updateDistrict(a, b);
            c.layoutDistrict(a, b);
            b.set_visible(!0)
        }, function() {
            b.set_visible(!0)
        })
    },
    context: function(a, b) {
        var c = this,
            d = ia.getMenu();
        d.addItem("Edit name", function() {
            c.edit(a, b)
        });
        d.addItem("Reroll all", (G = this.model, l(G, G.rerollDistricts)))
    },
    exportPNG: function(a) {
        if (a)
            for (a = this.labels.iterator(); a.hasNext();) {
                var b =
                    a.next();
                b.set_filters([]);
                b.addOutline()
            } else {
                a = !1;
                null == a && (a = !0);
                a = 2 * K.getStrokeWidth(K.strokeThick, a);
                var c = new Sc(K.colorPaper, 1, a, a, 100);
                for (a = this.labels.iterator(); a.hasNext();) b = a.next(), b.removeOutline(), b.set_filters([c])
            }
    },
    __class__: oi
});
var Ci = function() {
    this.resized = new Nc;
    U.call(this);
    this.frame = new Bi;
    this.add(this.frame);
    this.vbox = new gb;
    this.add(this.vbox);
    this.layout()
};
g["com.watabou.mfcg.scenes.overlays.Legend"] = Ci;
Ci.__name__ = "com.watabou.mfcg.scenes.overlays.Legend";
Ci.__super__ =
    U;
Ci.prototype = v(U.prototype, {
    layout: function() {
        this.frame.outline(this.vbox);
        this.rWidth = this.frame.get_width();
        this.rHeight = this.frame.get_height();
        var a = this.get_graphics();
        a.clear();
        a.beginFill(K.colorLight);
        a.drawRect(0, 0, this.rWidth, this.rHeight)
    },
    relayout: function() {
        this.vbox.layout();
        this.layout();
        this.resized.dispatch()
    },
    addView: function(a) {
        this.vbox.add(a)
    },
    addItem: function(a, b) {
        a = new kh(this, a, b);
        this.addView(a);
        return a
    },
    addTitle: function(a) {
        a = new lh(this, a);
        this.addView(a);
        return a
    },
    addEmblem: function() {
        var a = new mh(this);
        this.addView(a);
        return a
    },
    addSeparator: function() {
        this.addView(new nh)
    },
    addScale: function() {
        this.scale = new oh(this);
        this.scale.update();
        this.addView(this.scale)
    },
    wipe: function() {
        this.vbox.wipe();
        this.relayout()
    },
    __class__: Ci
});
var ug = function() {};
g["com.watabou.mfcg.scenes.overlays._Legend.Legendary"] = ug;
ug.__name__ = "com.watabou.mfcg.scenes.overlays._Legend.Legendary";
ug.__isInterface__ = !0;
var kh = function(a, b, c) {
    this.context = new Nc;
    this.click = new Nc;
    var d = this;
    U.call(this);
    this.legend = a;
    a = vb.getFormat("font_legend", vb.fontLegend, K.colorDark);
    var f = a.size,
        h = .5 * f;
    this.tfSymb = vb.get(b + ".", a);
    this.tfSymb.set_x(f - this.tfSymb.get_width());
    this.tfSymb.mouseEnabled = !1;
    this.addChild(this.tfSymb);
    this.tfDesc = vb.get(c, a);
    this.tfDesc.set_x(f + h);
    this.tfDesc.set_selectable(!1);
    this.addChild(this.tfDesc);
    this.rWidth = f + h + this.tfDesc.get_width();
    this.rHeight = this.tfDesc.get_height();
    this.tfDesc.addEventListener("mouseDown", function(a) {
        d.click.dispatch()
    });
    this.tfDesc.addEventListener("rightClick",
        function(a) {
            d.context.dispatch()
        })
};
g["com.watabou.mfcg.scenes.overlays._Legend.LegendItem"] = kh;
kh.__name__ = "com.watabou.mfcg.scenes.overlays._Legend.LegendItem";
kh.__interfaces__ = [ug];
kh.__super__ = U;
kh.prototype = v(U.prototype, {
    edit: function(a) {
        var b = this;
        We.fromTextField(this.tfDesc, this, 1, function(c) {
            "" == c && (c = "-");
            b.tfDesc.set_text(c);
            var d = b.tfDesc.get_x(),
                f = b.tfDesc.get_width();
            b.rWidth = d + f;
            b.legend.relayout();
            a(c)
        })
    },
    __class__: kh
});
var lh = function(a, b) {
    this.context = new Nc;
    this.click = new Nc;
    var c = this;
    U.call(this);
    this.legend = a;
    this.halign = "center";
    a = vb.getFormat("font_title", vb.fontTitle, K.colorDark, .6666666666666666);
    this.tf = vb.get(b, a);
    this.tf.set_selectable(!1);
    this.addChild(this.tf);
    this.rWidth = this.tf.get_width();
    this.rHeight = this.tf.get_height();
    this.tf.addEventListener("mouseDown", function(a) {
        c.click.dispatch()
    });
    this.tf.addEventListener("rightClick", function(a) {
        c.context.dispatch()
    })
};
g["com.watabou.mfcg.scenes.overlays._Legend.TitleView"] = lh;
lh.__name__ = "com.watabou.mfcg.scenes.overlays._Legend.TitleView";
lh.__interfaces__ = [ug];
lh.__super__ = U;
lh.prototype = v(U.prototype, {
    edit: function(a) {
        var b = this;
        We.fromTextField(this.tf, this, 0, function(c) {
            "" == c && (c = "-");
            b.tf.set_text(c);
            b.rWidth = b.tf.get_width();
            b.legend.relayout();
            null != a && a(c)
        })
    },
    __class__: lh
});
var nh = function() {
    U.call(this);
    this.halign = "fill";
    var a = !1;
    null == a && (a = !0);
    this.set_height(K.getStrokeWidth(K.strokeNormal, a))
};
g["com.watabou.mfcg.scenes.overlays._Legend.SeparatorView"] = nh;
nh.__name__ = "com.watabou.mfcg.scenes.overlays._Legend.SeparatorView";
nh.__interfaces__ = [ug];
nh.__super__ = U;
nh.prototype = v(U.prototype, {
    layout: function() {
        this.get_graphics().clear();
        this.get_graphics().beginFill(K.colorDark);
        this.get_graphics().drawRect(0, 0, this.rWidth, this.rHeight)
    },
    __class__: nh
});
var oh = function(a) {
    U.call(this);
    this.legend = a;
    this.halign = "center";
    this.scalebar = Lb.create(!0);
    this.addChild(this.scalebar);
    this.addEventListener("click", l(this, this.onClick));
    this.addEventListener("rightClick", l(this, this.onContext))
};
g["com.watabou.mfcg.scenes.overlays._Legend.ScaleBarView"] =
    oh;
oh.__name__ = "com.watabou.mfcg.scenes.overlays._Legend.ScaleBarView";
oh.__interfaces__ = [ug];
oh.__super__ = U;
oh.prototype = v(U.prototype, {
    update: function() {
        this.scalebar.update(ia.map);
        this.rWidth = this.scalebar.get_width();
        this.rHeight = this.scalebar.get_height() - 4;
        this.scalebar.set_y(this.rHeight)
    },
    onClick: function(a) {
        a.shiftKey ? (this.removeChild(this.scalebar), Lb.toggleView(), this.scalebar = Lb.create(!0), this.addChild(this.scalebar)) : Db.toggle();
        this.update();
        this.legend.relayout()
    },
    onContext: function(a) {
        var b =
            this,
            c = ia.getMenu();
        a = function(a, f) {
            c.addItem(a, function() {
                Db.set_current(f);
                b.scalebar.update();
                b.update();
                b.legend.relayout()
            }, Db.get_current() == f)
        };
        a("Metric units", Db.metric);
        a("Imperial units", Db.imperial);
        a = function(a, f) {
            c.addItem(a, function() {
                Lb.sbClass = f;
                b.removeChild(b.scalebar);
                b.scalebar = Lb.create(!0);
                b.addChild(b.scalebar);
                b.update();
                b.legend.relayout()
            }, Lb.sbClass == f)
        };
        a("Default style", of);
        a("Alternative style", vg);
        c.addItem("Hide", function() {
            ba.set("scale_bar", !1)
        })
    },
    __class__: oh
});
var mh = function(a) {
    U.call(this);
    this.legend = a;
    this.halign = "center";
    this.emblem = new ra;
    this.emblem.ready.add(l(this, this.layout));
    this.rWidth = this.emblem.get_width();
    this.rHeight = this.emblem.get_height();
    this.addChild(this.emblem);
    this.addEventListener("rightClick", l(this, this.onContext))
};
g["com.watabou.mfcg.scenes.overlays._Legend.EmblemView"] = mh;
mh.__name__ = "com.watabou.mfcg.scenes.overlays._Legend.EmblemView";
mh.__interfaces__ = [ug];
mh.__super__ = U;
mh.prototype = v(U.prototype, {
    layout: function() {
        this.rWidth =
            this.emblem.get_width();
        this.rHeight = this.emblem.get_height();
        this.legend.relayout()
    },
    onContext: function(a) {
        a = ia.getMenu();
        a.addItem("Customize...", (G = this.emblem, l(G, G.customize)));
        a.addItem("Reroll", (G = this.emblem, l(G, G.reroll)));
        a.addItem("Hide", function() {
            ba.set("emblem", !1)
        })
    },
    __class__: mh
});
var Uc = function(a) {
    this.legend = new Ci;
    this.legend.resized.add(l(this, this.layout));
    Ca.call(this, a);
    this.addChild(this.legend);
    Bb.titleChanged.add(l(this, this.onChangedStr));
    Bb.districtsChanged.add(l(this,
        this.onChangedVoid));
    Bb.landmarksChanged.add(l(this, this.onChangedVoid))
};
g["com.watabou.mfcg.scenes.overlays.LegendOverlay"] = Uc;
Uc.__name__ = "com.watabou.mfcg.scenes.overlays.LegendOverlay";
Uc.__super__ = Ca;
Uc.prototype = v(Ca.prototype, {
    layout: function() {
        Uc.auto && (Uc.pos = this.getAutoPos());
        var a = this.pos2point(Uc.pos);
        this.legend.set_x(a.x);
        this.legend.set_y(a.y);
        this.scene.arrangeOverlays()
    },
    get_position: function() {
        return Uc.pos
    },
    set_position: function(a) {
        Uc.auto = !1;
        Uc.pos != a && (Uc.pos = a, this.layout());
        return a
    },
    getAutoPos: function() {
        var a = this,
            b = ia.map;
        return null == b ? yc.TOP_LEFT : Z.min([yc.TOP_LEFT, yc.BOTTOM_LEFT, yc.BOTTOM_RIGHT, yc.TOP_RIGHT], function(c) {
            var d = a.pos2point(c);
            c = new I(d.x + a.legend.get_width(), d.y + a.legend.get_height());
            d = a.layer2map(b, d);
            c = a.layer2map(b, c);
            c = new na(d.x, d.y, c.x - d.x, c.y - d.y);
            return a.model.getDetails(c)
        })
    },
    pos2point: function(a) {
        switch (a._hx_index) {
            case 1:
                return new I(Ca.MARGIN, Ca.MARGIN);
            case 2:
                return new I(this.rWidth - this.legend.get_width() - Ca.MARGIN, Ca.MARGIN);
            case 3:
                return new I(Ca.MARGIN,
                    this.rHeight - this.legend.get_height() - Ca.MARGIN);
            case 4:
                return new I(this.rWidth - this.legend.get_width() - Ca.MARGIN, this.rHeight - this.legend.get_height() - Ca.MARGIN);
            default:
                return null
        }
    },
    update: function(a) {
        Ca.prototype.update.call(this, a);
        if (this.get_visible()) {
            this.legend.wipe();
            var b = ia.getMenu(),
                c = ba.get("city_name", 1),
                d = ba.get("scale_bar", !0),
                f = ba.get("emblem", !1),
                h = "Legend" == ba.get("districts", "Curved") && 0 < a.districts.length,
                k = "Legend" == ba.get("landmarks") && 0 < a.landmarks.length;
            if (c) {
                c = this.legend.addTitle(a.name);
                var n = function() {
                    c.edit(function(b) {
                        a.setName(b)
                    })
                };
                c.click.add(n);
                c.context.add(function() {
                    b.addItem("Edit name", n);
                    b.addItem("Reroll", function() {
                        a.setName(a.rerollName())
                    })
                });
                !h && !k || d || f || this.legend.addSeparator()
            }
            f && this.legend.addEmblem();
            d && this.legend.addScale();
            if (h) {
                d = 0;
                for (f = a.districts.length; d < f;) {
                    h = d++;
                    var p = [a.districts[h]];
                    h = Of.number(h);
                    var g = [function(a) {
                        return function(b) {
                            return a[0].name = b
                        }
                    }(p)];
                    h = [this.legend.addItem(h, p[0].name)];
                    p = [function(a, b) {
                        return function() {
                            a[0].edit(b[0])
                        }
                    }(h,
                        g)];
                    h[0].click.add(p[0]);
                    h[0].context.add(function(c) {
                        return function() {
                            b.addItem("Edit name", c[0]);
                            b.addItem("Reroll all", l(a, a.rerollDistricts))
                        }
                    }(p))
                }
                k && this.legend.addSeparator()
            }
            if (k)
                for (d = 0, f = a.landmarks.length; d < f;) h = d++, k = [a.landmarks[h]], h = Pf.letter(h), p = [function(a) {
                    return function(b) {
                        return a[0].name = b
                    }
                }(k)], h = [this.legend.addItem(h, k[0].name)], p = [function(a, b) {
                    return function() {
                        a[0].edit(b[0])
                    }
                }(h, p)], h[0].click.add(p[0]), h[0].context.add(function(c, d) {
                    return function() {
                        b.addItem("Edit name",
                            c[0]);
                        b.addItem("Delete", function(b) {
                            return function() {
                                a.removeLandmark(b[0])
                            }
                        }(d))
                    }
                }(p, k));
            this.legend.layout();
            this.layout()
        }
    },
    onContext: function(a) {
        var b = this,
            c = new dd,
            d = yc.TOP_LEFT;
        c.addItem("Top-left", function() {
            b.set_position(d)
        }, b.get_position() == d && !Uc.auto);
        var f = yc.TOP_RIGHT;
        c.addItem("Top-right", function() {
            b.set_position(f)
        }, b.get_position() == f && !Uc.auto);
        var h = yc.BOTTOM_LEFT;
        c.addItem("Bottom-left", function() {
            b.set_position(h)
        }, b.get_position() == h && !Uc.auto);
        var k = yc.BOTTOM_RIGHT;
        c.addItem("Bottom-right",
            function() {
                b.set_position(k)
            }, b.get_position() == k && !Uc.auto);
        c.addItem("Auto", function() {
            Uc.auto = !0;
            b.layout()
        }, Uc.auto);
        a.addSeparator();
        a.addSubmenu("Position", c);
        a.addItem("Hide", function() {
            "Legend" == ba.get("landmarks") && ba.set("landmarks", "Icon");
            "Legend" == ba.get("districts", "Curved") ? ba.set("districts", "Curved") : ia.inst.toggleOverlays()
        })
    },
    onChangedStr: function(a) {
        this.update(this.model)
    },
    onChangedVoid: function() {
        this.update(this.model)
    },
    onDestroy: function() {
        Ca.prototype.onDestroy.call(this);
        Bb.titleChanged.remove(l(this, this.onChangedStr));
        Bb.districtsChanged.remove(l(this, this.onChangedVoid))
    },
    __class__: Uc
});
var Pf = function(a) {
    this.moved = !1;
    ka.call(this);
    this.overlay = a;
    this.draw();
    this.set_buttonMode(!0);
    this.addEventListener("click", l(this, this.onClick));
    this.addEventListener("rightClick", l(this, this.onContext));
    this.addEventListener("rollOver", l(this, this.onRollOver));
    this.addEventListener("mouseDown", l(this, this.onMouseDown))
};
g["com.watabou.mfcg.scenes.overlays.Marker"] = Pf;
Pf.__name__ =
    "com.watabou.mfcg.scenes.overlays.Marker";
Pf.letter = function(a) {
    a = N.cca("a", 0) + a;
    return String.fromCodePoint(a)
};
Pf.__super__ = ka;
Pf.prototype = v(ka.prototype, {
    set: function(a, b) {
        this.landmark = a;
        this.set_symbol(b)
    },
    draw: function() {
        var a = vb.getFormat("font_pin", vb.fontPin, K.colorLabel, 1.1428571428571428),
            b = .6666666666666666 * a.size,
            c = .16666666666666666 * Math.PI,
            d = Math.cos(c),
            f = Math.sin(c);
        c = b / f;
        var h = this.get_graphics(),
            k = !1;
        null == k && (k = !0);
        h.lineStyle(2 * K.getStrokeWidth(K.strokeNormal, k), K.colorPaper);
        h.drawCircle(0, -c, b);
        h.moveTo(-b * d, -c + b * f);
        h.lineTo(0, 0);
        h.lineTo(b * d, -c + b * f);
        h.endFill();
        h.beginFill(K.colorLabel);
        h.drawCircle(0, -c, b);
        h.beginFill(K.colorLabel);
        h.moveTo(-b * d, -c + b * f);
        h.lineTo(0, 0);
        h.lineTo(b * d, -c + b * f);
        h.beginFill(K.colorPaper);
        d = this.get_graphics();
        k = !1;
        null == k && (k = !0);
        d.drawCircle(0, -c, b - K.getStrokeWidth(K.strokeThick, k));
        this.tf = vb.get("a", a);
        this.tf.mouseEnabled = !1;
        this.tf.set_y(-c - this.tf.get_height() / 2);
        this.addChild(this.tf)
    },
    set_symbol: function(a) {
        this.tf.set_text(a);
        this.tf.set_x(-this.tf.get_width() /
            2);
        return a
    },
    onClick: function(a) {
        this.moved || (this.overlay.editMarker(this.landmark), a.stopPropagation())
    },
    onRollOver: function(a) {
        le.inst.set(this.landmark.name)
    },
    onMouseDown: function(a) {
        this.stage.addEventListener("mouseMove", l(this, this.onMouseMove));
        this.stage.addEventListener("mouseUp", l(this, this.onMouseUp));
        this.grabX = this.get_mouseX();
        this.grabY = this.get_mouseY();
        this.moved = !1
    },
    onMouseMove: function(a) {
        this.moved = !0;
        this.set_x(this.parent.get_mouseX() - this.grabX);
        this.set_y(this.parent.get_mouseY() -
            this.grabY);
        a.updateAfterEvent()
    },
    onMouseUp: function(a) {
        this.stage.removeEventListener("mouseMove", l(this, this.onMouseMove));
        this.stage.removeEventListener("mouseUp", l(this, this.onMouseUp));
        this.overlay.onDrag(this)
    },
    onContext: function(a) {
        this.overlay.landmark = this.landmark
    },
    __class__: Pf,
    __properties__: v(ka.prototype.__properties__, {
        set_symbol: "set_symbol"
    })
});
var mi = function(a) {
    var b = this;
    Ca.call(this, a);
    Bb.landmarksChanged.add(function() {
        b.update(b.model)
    })
};
g["com.watabou.mfcg.scenes.overlays.MarkersOverlay"] =
    mi;
mi.__name__ = "com.watabou.mfcg.scenes.overlays.MarkersOverlay";
mi.__super__ = Ca;
mi.prototype = v(Ca.prototype, {
    layout: function() {
        Ca.prototype.layout.call(this);
        this.sync()
    },
    sync: function() {
        for (var a = 0, b = this.get_numChildren(); a < b;) {
            var c = a++;
            c = this.getChildAt(c);
            var d = this.map2layer(ia.map, c.landmark.pos);
            c.set_x(d.x);
            c.set_y(d.y)
        }
    },
    update: function(a) {
        Ca.prototype.update.call(this, a);
        for (var b = 0, c = a.landmarks.length; b < c;) {
            var d = b++,
                f = a.landmarks[d],
                h = this.getChildAt(d);
            null == h && (h = new Pf(this), this.addChild(h));
            h.set(f, Pf.letter(d));
            null != ia.map && (d = this.map2layer(ia.map, f.pos), h.set_x(d.x), h.set_y(d.y))
        }
        for (; this.get_numChildren() > a.landmarks.length;) this.removeChildAt(a.landmarks.length)
    },
    editMarker: function(a) {
        u.showDialog(new hh(a, !1))
    },
    onDrag: function(a) {
        var b = ia.map,
            c = a.get_x(),
            d = a.get_y();
        a.landmark.pos = this.layer2map(b, new I(c, d));
        a.landmark.assign()
    },
    onContext: function(a) {
        var b = this;
        a.addItem("Edit", function() {
            b.editMarker(b.landmark)
        });
        a.addItem("Hide", function() {
            ba.set("landmarks", "Hidden")
        })
    },
    __class__: mi
});
var li = function(a) {
    Ca.call(this, a);
    this.mouseChildren = !1
};
g["com.watabou.mfcg.scenes.overlays.PinsOverlay"] = li;
li.__name__ = "com.watabou.mfcg.scenes.overlays.PinsOverlay";
li.__super__ = Ca;
li.prototype = v(Ca.prototype, {
    layout: function() {
        Ca.prototype.layout.call(this);
        this.sync()
    },
    sync: function() {
        for (var a = 0, b = this.get_numChildren(); a < b;) {
            var c = a++;
            c = this.getChildAt(c);
            var d = this.map2layer(ia.map, c.pos);
            c.set_x(d.x);
            c.set_y(d.y)
        }
    },
    update: function(a) {
        for (var b = 0, c = a.districts.length; b < c;) {
            var d =
                b++,
                f = a.districts[d];
            d = Of.number(d);
            null == f.equator && (f.equator = ze.build(Ua.toPoly(f.border)));
            this.addChild(new Of(d, qa.lerp(f.equator[0], f.equator[1])))
        }
    },
    __class__: li
});
var Of = function(a, b) {
    ka.call(this);
    this.pos = b;
    b = vb.getFormat("font_pin", vb.fontPin, K.colorPaper);
    this.tf = vb.get(a, b);
    this.tf.set_x(-this.tf.get_width() / 2);
    this.tf.set_y(-this.tf.get_height() / 2);
    this.addChild(this.tf);
    a = b.size;
    this.get_graphics().beginFill(K.colorPaper);
    this.get_graphics().drawCircle(0, 0, a);
    this.get_graphics().beginFill(K.colorLabel);
    b = this.get_graphics();
    var c = !1;
    null == c && (c = !0);
    b.drawCircle(0, 0, a - K.getStrokeWidth(K.strokeThick, c))
};
g["com.watabou.mfcg.scenes.overlays.Pin"] = Of;
Of.__name__ = "com.watabou.mfcg.scenes.overlays.Pin";
Of.number = function(a) {
    return H.string(a + 1)
};
Of.__super__ = ka;
Of.prototype = v(ka.prototype, {
    __class__: Of
});
var Lb = function() {
    this.scale = 0;
    ka.call(this);
    this.format = vb.getFormat("font_element", vb.fontElement, K.colorDark)
};
g["com.watabou.mfcg.scenes.overlays.ScaleBar"] = Lb;
Lb.__name__ = "com.watabou.mfcg.scenes.overlays.ScaleBar";
Lb.toggleView = function() {
    Lb.sbClass = Lb.sbClass != of ? of : vg
};
Lb.create = function(a) {
    null == a && (a = !1);
    null == Lb.sbClass && (Lb.sbClass = of);
    return w.createInstance(Lb.sbClass, [a])
};
Lb.__super__ = ka;
Lb.prototype = v(ka.prototype, {
    update: function(a) {
        this.get_graphics().clear();
        var b = this.getMinSize();
        null != a && (this.scale = this.getScale(a));
        for (Lb.units = Db.get_current();;)
            if (a = Lb.units.iu2unit * this.scale, this.tickUnit = Math.pow(10, Math.ceil(Math.log(b / a) / Math.log(10))), this.tickPx = this.tickUnit * a, this.tickPx > 5 * b ? (this.tickUnit /=
                    5, this.tickPx /= 5) : this.tickPx > 4 * b ? (this.tickUnit /= 4, this.tickPx /= 4) : this.tickPx > 2 * b && (this.tickUnit /= 2, this.tickPx /= 2), 1 >= this.tickUnit && null != Lb.units.sub) Lb.units = Lb.units.sub;
            else break
    },
    getMinSize: function() {
        return 100
    },
    createLabel: function(a) {
        null == a && (a = "");
        a = vb.get(a, this.format);
        a.set_selectable(!1);
        return a
    },
    getScale: function(a) {
        for (var b = 1, c = this; null != c;) b /= c.get_scaleX(), c = c.parent;
        for (c = a; null != c;) b *= c.get_scaleX(), c = c.parent;
        return b
    },
    __class__: Lb
});
var vg = function(a) {
    null == a && (a = !1);
    this.embeded = a;
    Lb.call(this);
    this.black = K.colorDark;
    this.white = K.colorLight;
    this.grey = K.colorDark;
    this.tfValues = [];
    var b = this.createLabel();
    this.tfValues.push(b);
    this.addChild(b);
    b = this.createLabel();
    this.tfValues.push(b);
    this.addChild(b);
    b = this.createLabel();
    this.tfValues.push(b);
    this.addChild(b);
    b = this.createLabel();
    this.tfValues.push(b);
    this.addChild(b);
    b = this.createLabel();
    this.tfValues.push(b);
    a || this.addChild(b);
    this.tfUnits = this.createLabel();
    this.addChild(this.tfUnits)
};
g["com.watabou.mfcg.scenes.overlays.ScaleBarNew"] =
    vg;
vg.__name__ = "com.watabou.mfcg.scenes.overlays.ScaleBarNew";
vg.__super__ = Lb;
vg.prototype = v(Lb.prototype, {
    update: function(a) {
        Lb.prototype.update.call(this, a);
        a = (this.tickPx - 2) / 5;
        for (var b = 0; 5 > b;) {
            var c = b++,
                d = this.tfValues[c];
            d.set_text(H.string(this.tickUnit * (c + 1) / 5));
            d.set_x(this.tickPx * (c + 1) / 5 - d.get_width() / 2);
            d.set_y(-12 - d.get_height() + 2);
            d = 0 == (c & 1) ? this.white : this.grey;
            this.get_graphics().beginFill(d);
            this.get_graphics().drawRect(a * c + 1, -8, a, 8)
        }
        d = this.get_graphics();
        a = !1;
        null == a && (a = !0);
        d.lineStyle(K.getStrokeWidth(K.strokeNormal,
            a), K.colorDark, 1, !0, null, 0, 1);
        this.get_graphics().moveTo(0, 0);
        this.get_graphics().lineTo(this.tickPx, 0);
        this.get_graphics().moveTo(0, -8);
        this.get_graphics().lineTo(this.tickPx, -8);
        this.get_graphics().moveTo(0, -12);
        this.get_graphics().lineTo(0, 0);
        this.get_graphics().moveTo(this.tickPx, -12);
        this.get_graphics().lineTo(this.tickPx, 0);
        a = this.embeded ? this.tfValues[3] : this.tfValues[4];
        this.tfUnits.set_text(Lb.units.unit);
        this.tfUnits.set_x(a.get_x() + a.get_width());
        this.tfUnits.set_y(a.get_y())
    },
    getMinSize: function() {
        return 180
    },
    __class__: vg
});
var of = function(a) {
    null == a && (a = !1);
    this.embeded = a;
    Lb.call(this);
    this.tfZero = this.createLabel("0");
    this.addChild(this.tfZero);
    this.tfHalf = this.createLabel();
    this.addChild(this.tfHalf);
    this.tfFull = this.createLabel();
    this.addChild(this.tfFull);
    this.tfUnits = this.createLabel();
    this.addChild(this.tfUnits)
};
g["com.watabou.mfcg.scenes.overlays.ScaleBarOld"] = of;
of.__name__ = "com.watabou.mfcg.scenes.overlays.ScaleBarOld";
of.__super__ = Lb;
of.prototype = v(Lb.prototype, {
    update: function(a) {
        Lb.prototype.update.call(this,
            a);
        this.get_graphics().beginFill(16711680, 0);
        this.get_graphics().drawRect(0, -12, (this.tickPx | 0) + 1, 12);
        this.get_graphics().endFill();
        a = this.get_graphics();
        var b = !1;
        null == b && (b = !0);
        a.lineStyle(K.getStrokeWidth(K.strokeNormal, b), K.colorDark, 1, !0, null, 0, 1);
        this.get_graphics().moveTo(0, -12);
        this.get_graphics().lineTo(0, 0);
        this.get_graphics().lineTo(this.tickPx, 0);
        this.get_graphics().lineTo(this.tickPx, -12);
        this.get_graphics().moveTo(.25 * this.tickPx, 0);
        this.get_graphics().lineTo(.25 * this.tickPx, -6);
        this.get_graphics().moveTo(.5 *
            this.tickPx, 0);
        this.get_graphics().lineTo(.5 * this.tickPx, -12);
        this.get_graphics().moveTo(.75 * this.tickPx, 0);
        this.get_graphics().lineTo(.75 * this.tickPx, -6);
        this.tfZero.set_x(this.embeded ? -2 : -this.tfZero.get_width() / 2);
        this.tfZero.set_y(-12 - this.tfZero.get_height());
        this.tfHalf.set_text(H.string(this.tickUnit / 2));
        this.tfHalf.set_x(this.tickPx / 2 - this.tfHalf.get_width() / 2);
        this.tfHalf.set_y(-12 - this.tfHalf.get_height());
        this.tfFull.set_text(H.string(this.tickUnit));
        this.tfFull.set_x(this.tickPx - (this.embeded ?
            this.tfFull.get_width() - 2 : this.tfFull.get_width() / 2));
        this.tfFull.set_y(-12 - this.tfFull.get_height());
        this.tfUnits.set_text(Lb.units.unit);
        this.tfUnits.set_x(this.embeded ? this.tfZero.get_x() + this.tfZero.get_width() : this.tfFull.get_x() + this.tfFull.get_width());
        this.tfUnits.set_y(this.tfFull.get_y())
    },
    getMinSize: function() {
        return 150
    },
    __class__: of
});
var ni = function(a) {
    this.scalebar = Lb.create();
    Ca.call(this, a);
    this.addChild(this.scalebar)
};
g["com.watabou.mfcg.scenes.overlays.ScaleBarOverlay"] = ni;
ni.__name__ =
    "com.watabou.mfcg.scenes.overlays.ScaleBarOverlay";
ni.__super__ = Ca;
ni.prototype = v(Ca.prototype, {
    onContext: function(a) {
        var b = this,
            c = function(c, f) {
                a.addItem(c, function() {
                    Db.set_current(f);
                    b.scalebar.update()
                }, Db.get_current() == f)
            };
        c("Metric units", Db.metric);
        c("Imperial units", Db.imperial);
        c = function(c, f) {
            a.addItem(c, function() {
                Lb.sbClass = f;
                b.replace()
            }, Lb.sbClass == f)
        };
        c("Default style", of);
        c("Alternative style", vg);
        a.addItem("Hide", function() {
            ba.set("scale_bar", !1)
        })
    },
    replace: function() {
        null != this.scalebar &&
            this.removeChild(this.scalebar);
        this.scalebar = Lb.create();
        this.addChild(this.scalebar);
        this.layout()
    },
    update: function(a) {
        this.scalebar.update(ia.map)
    },
    layout: function() {
        this.update(null);
        this.scalebar.set_x(Ca.MARGIN);
        this.scalebar.set_y(this.rHeight - Ca.MARGIN)
    },
    __class__: ni
});
var Di = function() {
    ka.call(this);
    this.text = new sc;
    this.text.set_defaultTextFormat(vb.getFormat("font_title", vb.fontTitle, K.colorDark));
    this.text.set_autoSize(1);
    this.addChild(this.text);
    this.mouseEnabled = !0;
    this.mouseChildren = !1;
    this.filterOn(!0);
    this.addEventListener("mouseDown", l(this, this.onClick))
};
g["com.watabou.mfcg.scenes.overlays.Title"] = Di;
Di.__name__ = "com.watabou.mfcg.scenes.overlays.Title";
Di.__super__ = ka;
Di.prototype = v(ka.prototype, {
    setText: function(a) {
        this.text.set_text(a);
        this.text.set_x(-this.text.get_width() / 2);
        this.text.set_y(-this.text.get_height() / 2)
    },
    edit: function(a) {
        var b = this;
        this.set_visible(!1);
        new We(a.name, this.text.get_defaultTextFormat(), new I(this.get_x(), this.get_y()), this.parent, null, function(c) {
            b.set_visible(!0);
            a.setName(c, !0)
        }, function() {
            b.set_visible(!0)
        })
    },
    onClick: function(a) {
        var b = Ub.instance;
        a.shiftKey ? b.setName(b.rerollName()) : this.edit(b)
    },
    filterOn: function(a) {
        null != this.outline && (this.removeChild(this.outline), this.outline = null);
        var b = !1;
        null == b && (b = !0);
        b = K.getStrokeWidth(K.strokeThick, b);
        a ? this.set_filters([new Sc(K.colorPaper, 1, 2 * b, 2 * b, 100)]) : (this.set_filters([]), this.outline = new jh(this.text, K.colorPaper, b, 8), this.addChildAt(this.outline, 0))
    },
    __class__: Di
});
var qi = function(a) {
    this.title = new Di;
    Ca.call(this, a);
    this.addChild(this.title);
    Bb.titleChanged.add(l(this, this.onChanged))
};
g["com.watabou.mfcg.scenes.overlays.TitleOverlay"] = qi;
qi.__name__ = "com.watabou.mfcg.scenes.overlays.TitleOverlay";
qi.__super__ = Ca;
qi.prototype = v(Ca.prototype, {
    layout: function() {
        this.title.set_x(this.rWidth / 2);
        this.title.set_y(Ca.MARGIN + this.title.get_height() / 2)
    },
    update: function(a) {
        Ca.prototype.update.call(this, a);
        this.title.setText(a.name)
    },
    onChanged: function(a) {
        this.title.setText(a)
    },
    onDestroy: function() {
        Ca.prototype.onDestroy.call(this);
        Bb.titleChanged.remove(l(this, this.onChanged))
    },
    onContext: function(a) {
        var b = this;
        a.addItem("Edit", function() {
            b.title.edit(b.model)
        });
        a.addItem("Reroll", function() {
            b.model.setName(b.model.rerollName());
            b.title.setText(b.model.name)
        });
        a.addItem("Hide", function() {
            ba.set("city_name", !1)
        })
    },
    exportPNG: function(a) {
        this.title.filterOn(!a)
    },
    __class__: qi
});
var Mb = function(a) {
    this.affectedPatches = [];
    this.affectedNodes = new pa;
    this.softness = .5;
    this.radius = 20;
    this.scene = a
};
g["com.watabou.mfcg.scenes.tools.WarpTool"] =
    Mb;
Mb.__name__ = "com.watabou.mfcg.scenes.tools.WarpTool";
Mb.prototype = {
    getName: function() {
        return "Unknown tool"
    },
    activate: function() {},
    onPress: function(a, b) {
        this.startx = this.prevx = this.curx = a;
        this.starty = this.prevy = this.cury = b
    },
    onRelease: function() {
        this.startx == this.curx && this.starty == this.cury || this.scene.submit(this.scene.model.cells, !0)
    },
    onMove: function(a, b) {},
    onDrag: function(a, b) {
        this.prevx = this.curx;
        this.prevy = this.cury;
        this.curx = a;
        this.cury = b
    },
    onWheel: function(a, b, c) {
        this.radius = Fc.gate(this.radius *
            Math.pow(1.05, 0 == c ? 0 : 0 > c ? -1 : 1), 2, 100);
        this.scene.updateBrush(this.radius, 1 - this.softness);
        this.onMove(a, b)
    },
    onKey: function(a) {
        switch (a) {
            case 107:
            case 187:
                return this.radius = Fc.gate(1.05 * this.radius, 2, 100), this.scene.updateBrush(this.radius, 1 - this.softness), !0;
            case 109:
            case 189:
                return this.radius = Fc.gate(this.radius / 1.05, 2, 100), this.scene.updateBrush(this.radius, 1 - this.softness), !0;
            default:
                return !1
        }
    },
    affect: function(a) {
        this.affectedNodes = new pa;
        this.affectedPatches = [];
        for (var b = this.scene.nodePatches.keys(); b.hasNext();) {
            var c =
                b.next(),
                d = I.distance(a, c);
            d < this.radius && (this.affectedNodes.set(c, Math.min(1, (1 - d / this.radius) / this.softness)), Z.addAll(this.affectedPatches, this.scene.nodePatches.h[c.__id__]))
        }
    },
    updateMesh: function() {
        var a = this;
        this.scene.clearMesh();
        for (var b = 0, c = this.affectedPatches; b < c.length;) {
            var d = c[b];
            ++b;
            kf.forEdge(d.shape, function(b, c) {
                a.scene.drawEdge(b, c, 2 * ((null != a.affectedNodes.h.__keys__[b.__id__] ? a.affectedNodes.h[b.__id__] : 0) + (null != a.affectedNodes.h.__keys__[c.__id__] ? a.affectedNodes.h[c.__id__] :
                    0)) / 2)
            })
        }
        for (b = this.affectedNodes.keys(); b.hasNext();) c = b.next(), this.scene.drawNode(c, 4 * this.affectedNodes.h[c.__id__])
    },
    __class__: Mb
};
var me = function(a) {
    this.modifiedPatches = [];
    Mb.call(this, a)
};
g["com.watabou.mfcg.scenes.tools.BloatTool"] = me;
me.__name__ = "com.watabou.mfcg.scenes.tools.BloatTool";
me.__super__ = Mb;
me.prototype = v(Mb.prototype, {
    getName: function() {
        return "Bloat"
    },
    activate: function() {
        this.radius = me.brushRadius;
        this.softness = 0;
        this.scene.updateBrush(this.radius, 1 - this.softness)
    },
    onMove: function(a,
        b) {
        this.affect(new I(a, b));
        this.updateMesh()
    },
    onDrag: function(a, b) {
        Mb.prototype.onDrag.call(this, a, b);
        var c = this.curx - this.prevx,
            d = this.cury - this.prevy;
        c = Math.min(1, .5 * Math.sqrt(c * c + d * d) / this.radius);
        a = new I(a, b);
        this.bloat(a, c);
        Z.addAll(this.modifiedPatches, this.affectedPatches);
        this.affect(a);
        this.updateMesh()
    },
    bloat: function(a, b) {
        for (var c = this.affectedNodes, d = c.keys(); d.hasNext();) {
            var f = d.next();
            c.get(f);
            var h = f.subtract(a),
                k = Math.pow(I.distance(f, a) / this.radius, -b);
            h.x *= k;
            h.y *= k;
            h.x += a.x;
            h.y +=
                a.y;
            wd.set(f, h)
        }
    },
    onPress: function(a, b) {
        Mb.prototype.onPress.call(this, a, b);
        this.modifiedPatches = []
    },
    onRelease: function() {
        me.brushRadius = this.radius;
        this.scene.submit(this.modifiedPatches, !0)
    },
    __class__: me
});
var Kf = function(a) {
    Mb.call(this, a)
};
g["com.watabou.mfcg.scenes.tools.DisplaceTool"] = Kf;
Kf.__name__ = "com.watabou.mfcg.scenes.tools.DisplaceTool";
Kf.__super__ = Mb;
Kf.prototype = v(Mb.prototype, {
    getName: function() {
        return "Displace"
    },
    activate: function() {
        this.radius = Kf.brushRadius;
        this.scene.updateBrush(this.radius,
            1 - this.softness)
    },
    onMove: function(a, b) {
        this.affect(new I(a, b));
        this.updateMesh()
    },
    onDrag: function(a, b) {
        Mb.prototype.onDrag.call(this, a, b);
        a = this.curx - this.prevx;
        b = this.cury - this.prevy;
        for (var c = this.affectedNodes, d = c.keys(); d.hasNext();) {
            var f = d.next(),
                h = c.get(f);
            f.x += a * h;
            f.y += b * h
        }
        this.updateMesh()
    },
    onPress: function(a, b) {
        Mb.prototype.onPress.call(this, a, b);
        this.scene.updateBrush(0)
    },
    onRelease: function() {
        Kf.brushRadius = this.radius;
        this.scene.updateBrush(this.radius, 1 - this.softness);
        if (this.startx !=
            this.curx || this.starty != this.cury) {
            for (var a = !1, b = this.affectedNodes.keys(); b.hasNext();) {
                var c = b.next();
                if (-1 != this.scene.model.shore.indexOf(c)) {
                    a = !0;
                    break
                }
            }
            this.scene.submit(this.affectedPatches, a)
        }
    },
    __class__: Kf
});
var xi = function(a) {
    Mb.call(this, a)
};
g["com.watabou.mfcg.scenes.tools.EqualizeTool"] = xi;
xi.__name__ = "com.watabou.mfcg.scenes.tools.EqualizeTool";
xi.__super__ = Mb;
xi.prototype = v(Mb.prototype, {
    getName: function() {
        return "Equalize"
    },
    activate: function() {
        this.patch = null;
        this.scene.updateBrush(0)
    },
    onMove: function(a, b) {
        a = this.scene.model.getCell(new I(a, b));
        if (this.patch != a) {
            this.patch = a;
            this.affectedNodes = new pa;
            this.affectedPatches = [];
            if (null != this.patch) {
                this.shape = this.patch.shape;
                this.len = this.shape.length;
                b = 0;
                for (var c = this.shape; b < c.length;) a = c[b], ++b, this.affectedNodes.set(a, 1), Z.addAll(this.affectedPatches, this.scene.nodePatches.h[a.__id__])
            }
            this.updateMesh()
        }
    },
    onDrag: function(a, b) {
        Mb.prototype.onDrag.call(this, a, b);
        null != this.patch && (a = this.curx - this.prevx, b = this.cury - this.prevy, a =
            Math.min(1, 2 * Math.sqrt(a * a + b * b) / Sa.perimeter(this.shape)), this.equalize(a), this.updateMesh())
    },
    equalize: function(a) {
        for (var b = Sa.centroid(this.shape), c = this.shape[0].subtract(b), d = 1, f = this.len; d < f;) {
            var h = d++,
                k = this.shape[h].subtract(b),
                n = 2 * -Math.PI * h / this.len,
                p = Math.sin(n);
            n = Math.cos(n);
            h = new I(k.x * n - k.y * p, k.y * n + k.x * p);
            c.x += h.x;
            c.y += h.y
        }
        d = 1 / this.len;
        c.x *= d;
        c.y *= d;
        d = 0;
        for (f = this.len; d < f;) h = d++, n = 2 * Math.PI * h / this.len, p = Math.sin(n), n = Math.cos(n), wd.set(this.shape[h], qa.lerp(this.shape[h], b.add(new I(c.x *
            n - c.y * p, c.y * n + c.x * p)), a))
    },
    onRelease: function() {
        for (var a = !1, b = this.affectedNodes.keys(); b.hasNext();) {
            var c = b.next();
            if (-1 != this.scene.model.shore.indexOf(c)) {
                a = !0;
                break
            }
        }
        this.scene.submit(this.affectedPatches, a);
        this.onMove(this.curx, this.cury)
    },
    updateMesh: function() {
        this.scene.clearMesh();
        if (null != this.patch)
            for (var a = 0, b = this.len; a < b;) {
                var c = a++,
                    d = this.shape[c];
                this.scene.drawEdge(d, this.shape[(c + 1) % this.len], 2);
                this.scene.drawNode(d, 4)
            }
    },
    __class__: xi
});
var Lf = function(a) {
    this.modifiedPatches = [];
    Mb.call(this, a)
};
g["com.watabou.mfcg.scenes.tools.LiquifyTool"] = Lf;
Lf.__name__ = "com.watabou.mfcg.scenes.tools.LiquifyTool";
Lf.__super__ = Mb;
Lf.prototype = v(Mb.prototype, {
    getName: function() {
        return "Liquify"
    },
    activate: function() {
        this.radius = Lf.brushRadius;
        this.softness = 0;
        this.scene.updateBrush(this.radius, 1 - this.softness)
    },
    onMove: function(a, b) {
        this.affect(new I(a, b));
        this.updateMesh()
    },
    onDrag: function(a, b) {
        Mb.prototype.onDrag.call(this, a, b);
        for (var c = this.curx - this.prevx, d = this.cury - this.prevy, f = this.affectedNodes,
                h = f.keys(); h.hasNext();) {
            var k = h.next(),
                n = f.get(k);
            k.x += c * n * .5;
            k.y += d * n * .5
        }
        Z.addAll(this.modifiedPatches, this.affectedPatches);
        this.affect(new I(a, b));
        this.updateMesh()
    },
    onPress: function(a, b) {
        Mb.prototype.onPress.call(this, a, b);
        this.modifiedPatches = []
    },
    onRelease: function() {
        Lf.brushRadius = this.radius;
        this.scene.submit(this.modifiedPatches, !0)
    },
    affect: function(a) {
        this.affectedNodes = new pa;
        this.affectedPatches = [];
        for (var b = this.scene.nodePatches.keys(); b.hasNext();) {
            var c = b.next(),
                d = I.distance(a, c);
            d < this.radius && (this.affectedNodes.set(c, 1 - d / this.radius), Z.addAll(this.affectedPatches, this.scene.nodePatches.h[c.__id__]))
        }
    },
    __class__: Lf
});
var wi = function(a) {
    Mb.call(this, a)
};
g["com.watabou.mfcg.scenes.tools.MeasureTool"] = wi;
wi.__name__ = "com.watabou.mfcg.scenes.tools.MeasureTool";
wi.__super__ = Mb;
wi.prototype = v(Mb.prototype, {
    getName: function() {
        return "Measure"
    },
    activate: function() {
        this.scene.updateBrush(0);
        this.scene.clearMesh()
    },
    onPress: function(a, b) {
        Mb.prototype.onPress.call(this, a, b);
        this.start =
            new I(a, b);
        this.cur = new I(a, b);
        this.updateMesh()
    },
    onRelease: function() {
        if (this.startx != this.curx || this.starty != this.cury) {
            var a = I.distance(this.start, this.cur);
            a = Db.get_current().measure(a);
            var b = a.value;
            q.show((10 > b ? Math.round(10 * b) / 10 : Math.round(b)) + " " + a.system.unit)
        } else this.scene.clearMesh()
    },
    onDrag: function(a, b) {
        Mb.prototype.onDrag.call(this, a, b);
        this.cur.setTo(a, b);
        this.updateMesh()
    },
    updateMesh: function() {
        this.scene.clearMesh();
        this.scene.drawEdge(this.start, this.cur, 2);
        this.scene.drawNode(this.start,
            4);
        this.scene.drawNode(this.cur, 4)
    },
    __class__: wi
});
var vi = function(a) {
    me.call(this, a)
};
g["com.watabou.mfcg.scenes.tools.PinchTool"] = vi;
vi.__name__ = "com.watabou.mfcg.scenes.tools.PinchTool";
vi.__super__ = me;
vi.prototype = v(me.prototype, {
    getName: function() {
        return "Pinch"
    },
    bloat: function(a, b) {
        me.prototype.bloat.call(this, a, -b)
    },
    __class__: vi
});
var Mf = function(a) {
    this.modifiedPatches = [];
    Mb.call(this, a)
};
g["com.watabou.mfcg.scenes.tools.RelaxTool"] = Mf;
Mf.__name__ = "com.watabou.mfcg.scenes.tools.RelaxTool";
Mf.__super__ = Mb;
Mf.prototype = v(Mb.prototype, {
    getName: function() {
        return "Relax"
    },
    activate: function() {
        this.radius = Mf.brushRadius;
        this.scene.updateBrush(this.radius, 1 - this.softness)
    },
    onMove: function(a, b) {
        this.affect(new I(a, b));
        this.updateMesh()
    },
    onDrag: function(a, b) {
        Mb.prototype.onDrag.call(this, a, b);
        var c = this.curx - this.prevx,
            d = this.cury - this.prevy;
        this.relax(Math.min(1, Math.sqrt(c * c + d * d) / this.radius));
        Z.addAll(this.modifiedPatches, this.affectedPatches);
        this.affect(new I(a, b));
        this.updateMesh()
    },
    relax: function(a) {
        var b =
            new pa,
            c = this.affectedNodes,
            d = c;
        for (c = c.keys(); c.hasNext();) {
            var f = c.next();
            d.get(f);
            var h = this.scene.model.dcel.vertices.h[f.__id__];
            f = new I;
            for (var k = !0, n = 0, p = h.edges; n < p.length;) {
                var g = p[n];
                ++n;
                if (null == g.twin) {
                    k = !1;
                    break
                } else g = g.next.origin.point, f.x += g.x, f.y += g.y
            }
            k && (k = h.point, h = 1 / h.edges.length, f.x *= h, f.y *= h, f.x -= k.x, f.y -= k.y, b.set(k, f))
        }
        d = c = b;
        for (c = c.keys(); c.hasNext();) f = c.next(), b = d.get(f), h = a * this.affectedNodes.h[f.__id__], f.x += b.x * h, f.y += b.y * h
    },
    onPress: function(a, b) {
        Mb.prototype.onPress.call(this,
            a, b);
        this.modifiedPatches = []
    },
    onRelease: function() {
        Mf.brushRadius = this.radius;
        this.scene.submit(this.modifiedPatches, !0)
    },
    __class__: Mf
});
var ui = function(a) {
    Mb.call(this, a);
    var b = [],
        c = 0;
    for (a = a.model.dcel.edges; c < a.length;) {
        var d = a[c];
        ++c;
        null != d.data && b.push(d)
    }
    this.segments = b
};
g["com.watabou.mfcg.scenes.tools.RotateTool"] = ui;
ui.__name__ = "com.watabou.mfcg.scenes.tools.RotateTool";
ui.__super__ = Mb;
ui.prototype = v(Mb.prototype, {
    getName: function() {
        return "Rotate"
    },
    activate: function() {
        this.scene.updateBrush(0);
        this.updateMesh()
    },
    onDrag: function(a, b) {
        Mb.prototype.onDrag.call(this, a, b);
        b = (new I(this.prevx, this.prevy)).get_length();
        var c = (new I(this.curx, this.cury)).get_length();
        a = (this.prevx * this.curx + this.prevy * this.cury) / (b * c);
        b = (this.cury * this.prevx - this.curx * this.prevy) / (b * c);
        for (c = this.scene.prevState.keys(); c.hasNext();) {
            var d = c.next();
            d.setTo(d.x * a - d.y * b, d.y * a + d.x * b)
        }
        this.updateMesh()
    },
    updateMesh: function() {
        this.scene.clearMesh();
        for (var a = 0, b = this.segments; a < b.length;) {
            var c = b[a];
            ++a;
            this.scene.drawEdge(c.origin.point,
                c.next.origin.point, 2)
        }
    },
    __class__: ui
});
var sc = function() {
    this.__renderedOnCanvasWhileOnDOM = this.__forceCachedBitmapUpdate = !1;
    this.__mouseScrollVCounter = 0;
    this.condenseWhite = !1;
    xa.call(this);
    this.__wordSelection = !1;
    this.__drawableType = 7;
    this.__selectionIndex = this.__caretIndex = -1;
    this.__displayAsPassword = !1;
    this.__graphics = new Ed(this);
    this.__textEngine = new Nb(this);
    this.__layoutDirty = !0;
    this.__offsetY = this.__offsetX = 0;
    this.__mouseWheelEnabled = !0;
    this.__text = "";
    null == sc.__defaultTextFormat && (sc.__defaultTextFormat =
        new we("Times New Roman", 12, 0, !1, !1, !1, "", "", 3, 0, 0, 0, 0), sc.__defaultTextFormat.blockIndent = 0, sc.__defaultTextFormat.bullet = !1, sc.__defaultTextFormat.letterSpacing = 0, sc.__defaultTextFormat.kerning = !1);
    this.__textFormat = sc.__defaultTextFormat.clone();
    this.__textEngine.textFormatRanges.push(new od(this.__textFormat, 0, 0));
    this.addEventListener("mouseDown", l(this, this.this_onMouseDown));
    this.addEventListener("focusIn", l(this, this.this_onFocusIn));
    this.addEventListener("focusOut", l(this, this.this_onFocusOut));
    this.addEventListener("keyDown", l(this, this.this_onKeyDown));
    this.addEventListener("mouseWheel", l(this, this.this_onMouseWheel))
};
g["openfl.text.TextField"] = sc;
sc.__name__ = "openfl.text.TextField";
sc.__super__ = xa;
sc.prototype = v(xa.prototype, {
    getCharBoundaries: function(a) {
        if (0 > a || a > this.__text.length - 1) return null;
        var b = new na;
        return this.__getCharBoundaries(a, b) ? b : null
    },
    getLineIndexOfChar: function(a) {
        if (0 > a || a > this.__text.length) return -1;
        this.__updateLayout();
        for (var b = this.__textEngine.layoutGroups.iterator(); b.hasNext();) {
            var c =
                b.next();
            if (c.startIndex <= a && c.endIndex >= a) return c.lineIndex
        }
        return -1
    },
    getLineLength: function(a) {
        this.__updateLayout();
        if (0 > a || a > this.__textEngine.numLines - 1) return 0;
        for (var b = -1, c = -1, d = this.__textEngine.layoutGroups.iterator(); d.hasNext();) {
            var f = d.next();
            if (f.lineIndex == a) - 1 == b && (b = f.startIndex);
            else if (f.lineIndex == a + 1) {
                c = f.startIndex;
                break
            }
        } - 1 == c && (c = this.__text.length);
        return c - b
    },
    getLineOffset: function(a) {
        this.__updateLayout();
        if (0 > a || a > this.__textEngine.numLines - 1) return -1;
        for (var b = this.__textEngine.layoutGroups.iterator(); b.hasNext();) {
            var c =
                b.next();
            if (c.lineIndex == a) return c.startIndex
        }
        return 0
    },
    getLineText: function(a) {
        this.__updateLayout();
        if (0 > a || a > this.__textEngine.numLines - 1) return null;
        for (var b = -1, c = -1, d = this.__textEngine.layoutGroups.iterator(); d.hasNext();) {
            var f = d.next();
            if (f.lineIndex == a) - 1 == b && (b = f.startIndex);
            else if (f.lineIndex == a + 1) {
                c = f.startIndex;
                break
            }
        } - 1 == c && (c = this.__text.length);
        return this.__textEngine.text.substring(b, c)
    },
    replaceSelectedText: function(a) {
        this.__replaceSelectedText(a, !1)
    },
    replaceText: function(a, b,
        c) {
        this.__replaceText(a, b, c, !1)
    },
    setSelection: function(a, b) {
        this.__selectionIndex = a;
        this.__caretIndex = b;
        this.__updateScrollV();
        this.__updateScrollH();
        null != this.stage && this.stage.get_focus() == this && (this.__stopCursorTimer(), this.__startCursorTimer())
    },
    setTextFormat: function(a, b, c) {
        null == c && (c = -1);
        null == b && (b = -1);
        var d = this.get_text().length; - 1 == b ? (-1 == c && (c = d), b = 0) : -1 == c && (c = b + 1);
        if (b != c) {
            if (0 > b || 0 >= c || c < b || b >= d || c > d) throw new $g;
            if (0 == b && c == d) {
                this.__textEngine.textFormatRanges.set_length(1);
                var f =
                    this.__textEngine.textFormatRanges.get(0);
                f.start = 0;
                f.end = d;
                f.format.__merge(a)
            } else {
                d = 0;
                for (var h; d < this.__textEngine.textFormatRanges.get_length();)
                    if (f = this.__textEngine.textFormatRanges.get(d), f.end <= b) ++d;
                    else if (f.start >= c) break;
                else if (f.start <= b && f.end >= c)
                    if (f.start == b && f.end == c) {
                        f.format = f.format.clone();
                        f.format.__merge(a);
                        break
                    } else if (f.start == b) h = new od(f.format.clone(), b, c), h.format.__merge(a), this.__textEngine.textFormatRanges.insertAt(d, h), f.start = c, d += 2;
                else {
                    f.end == c ? (h = new od(f.format.clone(),
                        b, c), h.format.__merge(a), this.__textEngine.textFormatRanges.insertAt(d + 1, h)) : (h = new od(f.format.clone(), b, c), h.format.__merge(a), this.__textEngine.textFormatRanges.insertAt(d + 1, h), h = new od(f.format.clone(), c, f.end), this.__textEngine.textFormatRanges.insertAt(d + 2, h));
                    f.end = b;
                    break
                } else if (f.start >= b && f.end <= c) f.start == b ? (f.format = f.format.clone(), f.format.__merge(a), f.end = c) : this.__textEngine.textFormatRanges.removeAt(d);
                else if (f.start > b && f.end > b) {
                    f.start = c;
                    break
                } else f.start < b && f.end <= c ? (h = new od(f.format.clone(),
                    b, c), h.format.__merge(a), this.__textEngine.textFormatRanges.insertAt(d + 1, h), f.end = b, d += 2) : (++d, Ga.warn("You found a bug in OpenFL's text code! Please save a copy of your project and create an issue on GitHub so we can fix this.", {
                    fileName: "openfl/text/TextField.hx",
                    lineNumber: 1610,
                    className: "openfl.text.TextField",
                    methodName: "setTextFormat"
                }))
            }
            this.__layoutDirty = this.__dirty = !0;
            this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty())
        }
    },
    __setStageReference: function(a) {
        this.__stopTextInput();
        xa.prototype.__setStageReference.call(this, a)
    },
    __allowMouseFocus: function() {
        return this.mouseEnabled
    },
    __caretBeginningOfLine: function() {
        this.__caretIndex = this.getLineOffset(this.getLineIndexOfChar(this.__caretIndex))
    },
    __caretBeginningOfNextLine: function() {
        var a = this.getLineIndexOfChar(this.__caretIndex);
        this.__caretIndex = a < this.__textEngine.numLines - 1 ? this.getLineOffset(a + 1) : this.__text.length
    },
    __caretBeginningOfPreviousLine: function() {
        var a = this.getLineIndexOfChar(this.__caretIndex);
        if (0 < a) {
            var b =
                this.getLineOffset(this.getLineIndexOfChar(this.__caretIndex));
            this.__caretIndex = this.__caretIndex == b ? this.getLineOffset(a - 1) : b
        }
    },
    __caretEndOfLine: function() {
        var a = this.getLineIndexOfChar(this.__caretIndex);
        this.__caretIndex = a < this.__textEngine.numLines - 1 ? this.getLineOffset(a + 1) - 1 : this.__text.length
    },
    __caretNextCharacter: function() {
        this.__caretIndex < this.__text.length && this.__caretIndex++
    },
    __caretNextLine: function() {
        var a = this.getLineIndexOfChar(this.__caretIndex);
        a < this.__textEngine.numLines - 1 &&
            (this.__caretIndex = this.__getCharIndexOnDifferentLine(this.get_caretIndex(), a + 1))
    },
    __caretPreviousCharacter: function() {
        0 < this.__caretIndex && this.__caretIndex--
    },
    __caretPreviousLine: function() {
        var a = this.getLineIndexOfChar(this.__caretIndex);
        0 < a && (this.__caretIndex = this.__getCharIndexOnDifferentLine(this.get_caretIndex(), a - 1))
    },
    __disableInput: function() {
        this.__inputEnabled && null != this.stage && (this.stage.window.__backend.setTextInputEnabled(!1), this.stage.window.onTextInput.remove(l(this, this.window_onTextInput)),
            this.stage.window.onKeyDown.remove(l(this, this.window_onKeyDown)), this.__inputEnabled = !1, this.__stopCursorTimer())
    },
    __dispatch: function(a) {
        if (2 == a.eventPhase && "mouseUp" == a.type) {
            var b = this.__getGroup(this.get_mouseX(), this.get_mouseY(), !0);
            null != b && (b = b.format.url, null != b && "" != b && (O.startsWith(b, "event:") ? this.dispatchEvent(new Ce("link", !0, !1, N.substr(b, 6, null))) : Ra.getURL(new If(b))))
        }
        return xa.prototype.__dispatch.call(this, a)
    },
    __enableInput: function() {
        if (null != this.stage) {
            var a = this.getBounds(this.stage);
            a = new Ld(a.x, a.y, a.width, a.height);
            this.stage.window.setTextInputRect(a);
            this.stage.window.__backend.setTextInputEnabled(!0);
            this.__inputEnabled || (this.stage.window.__backend.setTextInputEnabled(!0), this.stage.window.onTextInput.has(l(this, this.window_onTextInput)) || (this.stage.window.onTextInput.add(l(this, this.window_onTextInput)), this.stage.window.onKeyDown.add(l(this, this.window_onKeyDown))), this.__inputEnabled = !0, this.__stopCursorTimer(), this.__startCursorTimer())
        }
    },
    __getBounds: function(a, b) {
        this.__updateLayout();
        var c = na.__pool.get();
        c.copyFrom(this.__textEngine.bounds);
        c.offset(this.__offsetX, this.__offsetY);
        c.__transform(c, b);
        a.__expand(c.x, c.y, c.width, c.height);
        na.__pool.release(c)
    },
    __getCharBoundaries: function(a, b) {
        if (0 > a || a > this.__text.length - 1) return !1;
        this.__updateLayout();
        for (var c = this.__textEngine.layoutGroups.iterator(); c.hasNext();) {
            var d = c.next();
            if (a >= d.startIndex && a < d.endIndex) try {
                for (var f = d.offsetX, h = 0, k = a - d.startIndex; h < k;) {
                    var n = h++;
                    f += d.positions[n]
                }
                b.setTo(f, d.offsetY, d.positions[a - d.startIndex],
                    d.ascent + d.descent);
                return !0
            } catch (p) {
                Ta.lastError = p
            }
        }
        return !1
    },
    __getCharIndexOnDifferentLine: function(a, b) {
        if (0 > a || a > this.__text.length || 0 > b || b > this.__textEngine.numLines - 1) return -1;
        for (var c = null, d = null, f = this.__textEngine.layoutGroups.iterator(); f.hasNext();) {
            var h = f.next();
            if (a >= h.startIndex && a <= h.endIndex) {
                c = h.offsetX;
                for (var k = 0, n = a - h.startIndex; k < n;) {
                    var p = k++;
                    c += h.positions[p]
                }
                if (null != d) return this.__getPosition(c, d)
            }
            if (h.lineIndex == b) {
                d = h.offsetY + h.height / 2;
                h = 0;
                for (k = this.get_scrollV() -
                    1; h < k;) n = h++, d -= this.__textEngine.lineHeights.get(n);
                if (null != c) return this.__getPosition(c, d)
            }
        }
        return -1
    },
    __getCursor: function() {
        var a = this.__getGroup(this.get_mouseX(), this.get_mouseY(), !0);
        return null != a && "" != a.format.url ? "button" : this.__textEngine.selectable ? "ibeam" : null
    },
    __getGroup: function(a, b, c) {
        null == c && (c = !1);
        this.__updateLayout();
        a += this.get_scrollH();
        for (var d = 0, f = this.get_scrollV() - 1; d < f;) {
            var h = d++;
            b += this.__textEngine.lineHeights.get(h)
        }!c && b > this.__textEngine.textHeight && (b = this.__textEngine.textHeight);
        var k = !0;
        d = 0;
        for (f = this.__textEngine.layoutGroups.get_length(); d < f;) {
            h = d++;
            var n = this.__textEngine.layoutGroups.get(h);
            h = h < this.__textEngine.layoutGroups.get_length() - 1 ? this.__textEngine.layoutGroups.get(h + 1) : null;
            k && (b < n.offsetY && (b = n.offsetY), a < n.offsetX && (a = n.offsetX), k = !1);
            if (b >= n.offsetY && b <= n.offsetY + n.height || !c && null == h)
                if (a >= n.offsetX && a <= n.offsetX + n.width || !c && (null == h || h.lineIndex != n.lineIndex)) return n
        }
        return null
    },
    __getPosition: function(a, b) {
        b = this.__getGroup(a, b);
        if (null == b) return this.__text.length;
        for (var c = 0, d = 0, f = b.positions.length; d < f;) {
            var h = d++;
            c += b.positions[h];
            if (a <= b.offsetX + c) {
                if (a <= b.offsetX + (c - b.positions[h]) + b.positions[h] / 2) return b.startIndex + h;
                if (b.startIndex + h < b.endIndex) return b.startIndex + h + 1;
                break
            }
        }
        return b.endIndex
    },
    __getPositionByIdentifier: function(a, b, c) {
        a = this.__getPosition(a, b);
        c = c ? "\n" : " .,;:!?()[]{}<>/\\|-=+*&^%$#@~`'\"";
        b = this.__text.charAt(a);
        if (this.__specialSelectionInitialIndex <= a)
            for (; - 1 == c.indexOf(b) && a < this.__text.length;) ++a, b = this.__text.charAt(a);
        else {
            for (; - 1 ==
                c.indexOf(b) && 0 < a;) --a, b = this.__text.charAt(a);
            if (0 == a) return a;
            ++a
        }
        return a
    },
    __getOppositeIdentifierBound: function(a, b) {
        b = b ? "\n" : " .,;:!?()[]{}<>/\\|-=+*&^%$#@~`'\"";
        var c = this.__text.charAt(a);
        if (a <= this.__caretIndex) {
            for (; - 1 == b.indexOf(c) && 0 < a;) --a, c = this.__text.charAt(a);
            if (0 == a) return a;
            ++a
        } else
            for (; - 1 == b.indexOf(c) && a < this.__text.length;) ++a, c = this.__text.charAt(a);
        return a
    },
    __hitTest: function(a, b, c, d, f, h) {
        if (!h.get_visible() || this.__isMask || f && !this.mouseEnabled || null != this.get_mask() && !this.get_mask().__hitTestMask(a,
                b)) return !1;
        this.__getRenderTransform();
        this.__updateLayout();
        c = this.__renderTransform;
        f = c.a * c.d - c.b * c.c;
        var k = 0 == f ? -c.tx : 1 / f * (c.c * (c.ty - b) + c.d * (a - c.tx));
        c = this.__renderTransform;
        f = c.a * c.d - c.b * c.c;
        return this.__textEngine.bounds.contains(k, 0 == f ? -c.ty : 1 / f * (c.a * (b - c.ty) + c.b * (c.tx - a))) ? (null != d && d.push(h), !0) : !1
    },
    __hitTestMask: function(a, b) {
        this.__getRenderTransform();
        this.__updateLayout();
        var c = this.__renderTransform,
            d = c.a * c.d - c.b * c.c,
            f = 0 == d ? -c.tx : 1 / d * (c.c * (c.ty - b) + c.d * (a - c.tx));
        c = this.__renderTransform;
        d = c.a * c.d - c.b * c.c;
        return this.__textEngine.bounds.contains(f, 0 == d ? -c.ty : 1 / d * (c.a * (b - c.ty) + c.b * (c.tx - a))) ? !0 : !1
    },
    __replaceSelectedText: function(a, b) {
        null == b && (b = !0);
        null == a && (a = "");
        if ("" != a || this.__selectionIndex != this.__caretIndex) {
            var c = this.__caretIndex < this.__selectionIndex ? this.__caretIndex : this.__selectionIndex,
                d = this.__caretIndex > this.__selectionIndex ? this.__caretIndex : this.__selectionIndex;
            if (!(c == d && 0 < this.__textEngine.maxChars && this.__text.length == this.__textEngine.maxChars)) {
                c > this.__text.length &&
                    (c = this.__text.length);
                d > this.__text.length && (d = this.__text.length);
                if (d < c) {
                    var f = d;
                    d = c;
                    c = f
                }
                0 > c && (c = 0);
                this.__replaceText(c, d, a, b)
            }
        }
    },
    __replaceText: function(a, b, c, d) {
        if (!(b < a || 0 > a || b > this.__text.length || null == c)) {
            d && (c = this.__textEngine.restrictText(c), 0 < this.__textEngine.maxChars && (d = this.__textEngine.maxChars - this.__text.length + (b - a), 0 >= d ? c = "" : d < c.length && (c = N.substr(c, 0, d))));
            this.__updateText(this.__text.substring(0, a) + c + this.__text.substring(b));
            d = c.length - (b - a);
            for (var f = 0, h; f < this.__textEngine.textFormatRanges.get_length();) {
                h =
                    this.__textEngine.textFormatRanges.get(f);
                if (a == b) h.start == h.end ? 0 != h.start ? Ga.warn("You found a bug in OpenFL's text code! Please save a copy of your project and create an issue on GitHub so we can fix this.", {
                    fileName: "openfl/text/TextField.hx",
                    lineNumber: 2184,
                    className: "openfl.text.TextField",
                    methodName: "__replaceText"
                }) : h.end += d : h.end >= a && (h.start >= a ? (h.start += d, h.end += d) : h.start < a && h.end >= b && (h.end += d));
                else if (h.end > a)
                    if (h.start > b) h.start += d, h.end += d;
                    else if (h.start <= a && h.end > b) h.end += d;
                else if (h.start >=
                    a && h.end <= b) {
                    h = this.__textEngine.textFormatRanges;
                    h.__tempIndex = f--;
                    for (var k = 0, n = []; k < n.length;) {
                        var p = n[k++];
                        h.insertAt(h.__tempIndex, p);
                        h.__tempIndex++
                    }
                    h.splice(h.__tempIndex, 1)
                } else h.end > b && h.start > a && h.start <= b ? (h.start = a, h.end += d) : h.start < a && h.end > a && h.end <= b && (h.end = a);
                ++f
            }
            0 == this.__textEngine.textFormatRanges.get_length() ? this.__textEngine.textFormatRanges.push(new od(this.get_defaultTextFormat().clone(), 0, c.length)) : a == b && 0 < this.__textEngine.textFormatRanges.get(0).start ? this.__textEngine.textFormatRanges.unshift(new od(this.get_defaultTextFormat().clone(),
                0, this.__textEngine.textFormatRanges.get(0).start)) : a != b && this.__textEngine.textFormatRanges.get(this.__textEngine.textFormatRanges.get_length() - 1).end < this.__text.length && this.__textEngine.textFormatRanges.push(new od(this.get_defaultTextFormat().clone(), this.__textEngine.textFormatRanges.get(this.__textEngine.textFormatRanges.get_length() - 1).end, this.__text.length));
            this.__selectionIndex = this.__caretIndex = a + c.length;
            this.__layoutDirty = this.__dirty = !0;
            this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty())
        }
    },
    __startCursorTimer: function() {
        1 == this.get_type() ? (this.__inputEnabled && (this.__cursorTimer = Qf.delay(l(this, this.__startCursorTimer), 600), this.__showCursor = !this.__showCursor), this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty())) : this.get_selectable() && (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()))
    },
    __startTextInput: function() {
        0 > this.__caretIndex && (this.__selectionIndex = this.__caretIndex = this.__text.length);
        (S.__supportDOM ?
            this.__renderedOnCanvasWhileOnDOM : 1) && this.__enableInput()
    },
    __stopCursorTimer: function() {
        null != this.__cursorTimer && (this.__cursorTimer.stop(), this.__cursorTimer = null);
        this.__showCursor && (this.__showCursor = !1, this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()))
    },
    __stopTextInput: function() {
        (S.__supportDOM ? this.__renderedOnCanvasWhileOnDOM : 1) && this.__disableInput()
    },
    __updateLayout: function() {
        if (this.__layoutDirty) {
            var a = this.__textEngine.width;
            this.__textEngine.update();
            if (2 != this.__textEngine.autoSize) {
                if (this.__textEngine.width != a) switch (this.__textEngine.autoSize) {
                    case 0:
                        this.set_x(this.get_x() + (a - this.__textEngine.width) / 2);
                        break;
                    case 3:
                        this.set_x(this.get_x() + (a - this.__textEngine.width))
                }
                this.__textEngine.getBounds()
            }
            this.__layoutDirty = !1;
            this.setSelection(this.__selectionIndex, this.__caretIndex)
        }
    },
    __updateMouseDrag: function() {
        if (null != this.stage) {
            var a = this.getBounds(this);
            this.get_mouseX() > a.width - 1 ? this.set_scrollH(this.get_scrollH() + (Math.max(Math.min(.1 *
                (this.get_mouseX() - a.width), 10), 1) | 0)) : 1 > this.get_mouseX() && this.set_scrollH(this.get_scrollH() - (Math.max(Math.min(-.1 * this.get_mouseX(), 10), 1) | 0));
            this.__mouseScrollVCounter++;
            this.__mouseScrollVCounter > this.stage.get_frameRate() / 10 && (this.get_mouseY() > a.height - 2 ? this.set_scrollV(Math.min(this.get_scrollV() + Math.max(Math.min(.03 * (this.get_mouseY() - a.height), 5), 1), this.get_maxScrollV()) | 0) : 2 > this.get_mouseY() && this.set_scrollV(this.get_scrollV() - (Math.max(Math.min(-.03 * this.get_mouseY(), 5), 1) | 0)),
                this.__mouseScrollVCounter = 0);
            this.stage_onMouseMove(null)
        }
    },
    __updateScrollH: function() {
        this.__updateLayout();
        var a = this.getBounds(this);
        if (this.get_textWidth() <= a.width - 4) this.set_scrollH(0);
        else {
            var b = this.get_scrollH();
            if (0 == this.__caretIndex || this.getLineOffset(this.getLineIndexOfChar(this.__caretIndex)) == this.__caretIndex) b = 0;
            else {
                var c = na.__pool.get(),
                    d = !1;
                this.__caretIndex < this.__text.length && (d = this.__getCharBoundaries(this.__caretIndex, c));
                d || (this.__getCharBoundaries(this.__caretIndex - 1,
                    c), c.x += c.width);
                for (; c.x < b && 0 < b;) b -= 24;
                for (; c.x > b + a.width - 4;) b += 24;
                na.__pool.release(c)
            }
            0 < b && 1 != this.get_type() && (c = this.getLineLength(this.getLineIndexOfChar(this.__caretIndex)), this.get_scrollH() + a.width - 4 > c && this.set_scrollH(Math.ceil(c - a.width + 4)));
            0 > b ? this.set_scrollH(0) : b > this.get_maxScrollH() ? this.set_scrollH(this.get_maxScrollH()) : this.set_scrollH(b)
        }
    },
    __updateScrollV: function() {
        this.__updateLayout();
        if (this.get_textHeight() <= this.get_height() - 4) this.set_scrollV(1);
        else {
            var a = this.getLineIndexOfChar(this.__caretIndex); -
            1 == a && 0 < this.__caretIndex && (a = this.getLineIndexOfChar(this.__caretIndex - 1) + 1);
            if (a + 1 < this.get_scrollV()) this.set_scrollV(a + 1);
            else if (a + 1 > this.get_bottomScrollV()) {
                var b = 0;
                for (a >= this.__textEngine.lineHeights.get_length() && (a = this.__textEngine.lineHeights.get_length() - 1); 0 <= a;) {
                    b += this.__textEngine.lineHeights.get(a);
                    if (b > this.get_height() - 4) {
                        a += 0 > b - this.get_height() ? 1 : 2;
                        break
                    }--a
                }
                this.set_scrollV(a)
            } else this.set_scrollV(this.get_scrollV())
        }
    },
    __updateText: function(a) {
        S.__supportDOM && this.__renderedOnCanvasWhileOnDOM &&
            (this.__forceCachedBitmapUpdate = this.__text != a);
        this.__textEngine.set_text(a);
        this.__text = this.__textEngine.text;
        null != this.stage && this.stage.get_focus() == this ? (this.__text.length < this.__selectionIndex && (this.__selectionIndex = this.__text.length), this.__text.length < this.__caretIndex && (this.__caretIndex = this.__text.length)) : this.__isHTML ? this.__selectionIndex = this.__caretIndex = this.__text.length : this.__caretIndex = this.__selectionIndex = 0;
        if (!this.__displayAsPassword || S.__supportDOM && !this.__renderedOnCanvasWhileOnDOM) this.__textEngine.set_text(this.__text);
        else {
            a = "";
            for (var b = 0, c = this.get_text().length; b < c;) b++, a += "*";
            this.__textEngine.set_text(a)
        }
    },
    __updateTransforms: function(a) {
        xa.prototype.__updateTransforms.call(this, a);
        a = this.__renderTransform;
        var b = this.__offsetX,
            c = this.__offsetY;
        a.tx = b * a.a + c * a.c + a.tx;
        a.ty = b * a.b + c * a.d + a.ty
    },
    set_autoSize: function(a) {
        a != this.__textEngine.autoSize && (this.__layoutDirty = this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        return this.__textEngine.autoSize = a
    },
    set_background: function(a) {
        a !=
            this.__textEngine.background && (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        return this.__textEngine.background = a
    },
    set_backgroundColor: function(a) {
        a != this.__textEngine.backgroundColor && (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        return this.__textEngine.backgroundColor = a
    },
    set_border: function(a) {
        a != this.__textEngine.border && (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        return this.__textEngine.border = a
    },
    set_borderColor: function(a) {
        a != this.__textEngine.borderColor && (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        return this.__textEngine.borderColor = a
    },
    get_bottomScrollV: function() {
        this.__updateLayout();
        return this.__textEngine.get_bottomScrollV()
    },
    get_caretIndex: function() {
        return this.__caretIndex
    },
    get_defaultTextFormat: function() {
        return this.__textFormat.clone()
    },
    set_defaultTextFormat: function(a) {
        this.__textFormat.__merge(a);
        this.__dirty = this.__layoutDirty = !0;
        this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty());
        return a
    },
    get_displayAsPassword: function() {
        return this.__displayAsPassword
    },
    get_height: function() {
        this.__updateLayout();
        return this.__textEngine.height * Math.abs(this.get_scaleY())
    },
    set_height: function(a) {
        a != this.__textEngine.height && (this.__setTransformDirty(), this.__layoutDirty = this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()), this.__textEngine.height =
            a);
        return this.__textEngine.height * Math.abs(this.get_scaleY())
    },
    get_htmlText: function() {
        return this.__isHTML ? this.__htmlText : this.__text
    },
    set_htmlText: function(a) {
        if (null == a) throw a = new Cf("Error #2007: Parameter text must be non-null."), a.errorID = 2007, a;
        this.__isHTML && this.__text == a || (this.__layoutDirty = this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        this.__isHTML = !0;
        this.condenseWhite && (a = a.replace(/\s+/g, " "));
        this.__htmlText = a;
        a = La.parse(a, this.get_multiline(),
            this.__styleSheet, this.__textFormat, this.__textEngine.textFormatRanges);
        this.__updateText(a);
        return a
    },
    get_length: function() {
        return null != this.__text ? this.__text.length : 0
    },
    get_maxScrollH: function() {
        this.__updateLayout();
        return this.__textEngine.maxScrollH
    },
    get_maxScrollV: function() {
        this.__updateLayout();
        return this.__textEngine.get_maxScrollV()
    },
    get_mouseWheelEnabled: function() {
        return this.__mouseWheelEnabled
    },
    get_multiline: function() {
        return this.__textEngine.multiline
    },
    get_numLines: function() {
        this.__updateLayout();
        return this.__textEngine.numLines
    },
    set_restrict: function(a) {
        this.__textEngine.restrict != a && (this.__textEngine.set_restrict(a), this.__updateText(this.__text));
        return a
    },
    get_scrollH: function() {
        return this.__textEngine.scrollH
    },
    set_scrollH: function(a) {
        this.__updateLayout();
        a > this.__textEngine.maxScrollH && (a = this.__textEngine.maxScrollH);
        0 > a && (a = 0);
        a != this.__textEngine.scrollH && (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()), this.__textEngine.scrollH = a, this.dispatchEvent(new wa("scroll")));
        return this.__textEngine.scrollH
    },
    get_scrollV: function() {
        return this.__textEngine.get_scrollV()
    },
    set_scrollV: function(a) {
        this.__updateLayout();
        a > this.__textEngine.get_maxScrollV() && (a = this.__textEngine.get_maxScrollV());
        1 > a && (a = 1);
        if (a != this.__textEngine.get_scrollV() || 0 == this.__textEngine.get_scrollV()) this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()), this.__textEngine.set_scrollV(a), this.dispatchEvent(new wa("scroll"));
        return this.__textEngine.get_scrollV()
    },
    get_selectable: function() {
        return this.__textEngine.selectable
    },
    set_selectable: function(a) {
        a != this.__textEngine.selectable && 1 == this.get_type() && (null != this.stage && this.stage.get_focus() == this ? this.__startTextInput() : a || this.__stopTextInput());
        return this.__textEngine.selectable = a
    },
    get_tabEnabled: function() {
        return null == this.__tabEnabled ? 1 == this.__textEngine.type : this.__tabEnabled
    },
    get_text: function() {
        return this.__text
    },
    set_text: function(a) {
        if (null == a) throw a = new Cf("Error #2007: Parameter text must be non-null."),
            a.errorID = 2007, a;
        if (null != this.__styleSheet) return this.set_htmlText(a);
        if (this.__isHTML || this.__text != a) this.__layoutDirty = this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty());
        else return a;
        if (1 < this.__textEngine.textFormatRanges.get_length()) {
            var b = this.__textEngine.textFormatRanges,
                c = this.__textEngine.textFormatRanges.get_length() - 1;
            b.__tempIndex = 1;
            for (var d = 0, f = []; d < f.length;) {
                var h = f[d++];
                b.insertAt(b.__tempIndex, h);
                b.__tempIndex++
            }
            b.splice(b.__tempIndex,
                c)
        }
        b = this.__textEngine.textFormatRanges.get(0);
        b.format = this.__textFormat;
        b.start = 0;
        b.end = a.length;
        this.__isHTML = !1;
        this.__updateText(a);
        return a
    },
    get_textWidth: function() {
        this.__updateLayout();
        return this.__textEngine.textWidth
    },
    get_textHeight: function() {
        this.__updateLayout();
        return this.__textEngine.textHeight
    },
    get_type: function() {
        return this.__textEngine.type
    },
    set_type: function(a) {
        null != this.__styleSheet && (a = 0);
        a != this.__textEngine.type && (this.__textEngine.type = a, 1 == a ? (this.addEventListener("addedToStage",
            l(this, this.this_onAddedToStage)), this.this_onFocusIn(null), this.__textEngine.__useIntAdvances = !0) : (this.removeEventListener("addedToStage", l(this, this.this_onAddedToStage)), this.__stopTextInput(), this.__textEngine.__useIntAdvances = null), this.__layoutDirty = this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        return this.__textEngine.type
    },
    get_width: function() {
        this.__updateLayout();
        return this.__textEngine.width * Math.abs(this.__scaleX)
    },
    set_width: function(a) {
        a !=
            this.__textEngine.width && (this.__setTransformDirty(), this.__layoutDirty = this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()), this.__textEngine.width = a);
        return this.__textEngine.width * Math.abs(this.__scaleX)
    },
    set_wordWrap: function(a) {
        a != this.__textEngine.wordWrap && (this.__layoutDirty = this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
        return this.__textEngine.wordWrap = a
    },
    get_x: function() {
        return this.__transform.tx + this.__offsetX
    },
    set_x: function(a) {
        a != this.__transform.tx + this.__offsetX && this.__setTransformDirty();
        this.__transform.tx = a - this.__offsetX;
        return a
    },
    get_y: function() {
        return this.__transform.ty + this.__offsetY
    },
    set_y: function(a) {
        a != this.__transform.ty + this.__offsetY && this.__setTransformDirty();
        this.__transform.ty = a - this.__offsetY;
        return a
    },
    stage_onMouseMove: function(a) {
        if (null != this.stage && this.get_selectable() && 0 <= this.__selectionIndex && (this.__updateLayout(), a = this.__lineSelection ? this.__getPositionByIdentifier(this.get_mouseX() +
                this.get_scrollH(), this.get_mouseY(), !0) : this.__wordSelection ? this.__getPositionByIdentifier(this.get_mouseX() + this.get_scrollH(), this.get_mouseY(), !1) : this.__getPosition(this.get_mouseX() + this.get_scrollH(), this.get_mouseY()), a != this.__caretIndex)) {
            this.__caretIndex = a;
            if (this.__wordSelection || this.__lineSelection) this.__selectionIndex = this.__getOppositeIdentifierBound(this.__specialSelectionInitialIndex, this.__lineSelection);
            a = !0;
            S.__supportDOM && (this.__renderedOnCanvasWhileOnDOM && (this.__forceCachedBitmapUpdate = !0), a = !1);
            a && (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()))
        }
    },
    stage_onMouseUp: function(a) {
        a = a.currentTarget;
        a.removeEventListener("enterFrame", l(this, this.this_onEnterFrame));
        a.removeEventListener("mouseMove", l(this, this.stage_onMouseMove));
        a.removeEventListener("mouseUp", l(this, this.stage_onMouseUp));
        if (this.stage == a && a.get_focus() == this) {
            this.__getWorldTransform();
            this.__updateLayout();
            a = this.__lineSelection ? this.__getPositionByIdentifier(this.get_mouseX() +
                this.get_scrollH(), this.get_mouseY(), !0) : this.__wordSelection ? this.__getPositionByIdentifier(this.get_mouseX() + this.get_scrollH(), this.get_mouseY(), !1) : this.__getPosition(this.get_mouseX() + this.get_scrollH(), this.get_mouseY());
            var b = Math.max(this.__selectionIndex, a) | 0;
            this.__selectionIndex = Math.min(this.__selectionIndex, a) | 0;
            this.__caretIndex = b;
            this.__wordSelection = this.__lineSelection = !1;
            this.__inputEnabled && (this.this_onFocusIn(null), this.__stopCursorTimer(), this.__startCursorTimer(), S.__supportDOM &&
                this.__renderedOnCanvasWhileOnDOM && (this.__forceCachedBitmapUpdate = !0))
        }
    },
    this_onAddedToStage: function(a) {
        this.this_onFocusIn(null)
    },
    this_onEnterFrame: function(a) {
        this.__updateMouseDrag()
    },
    this_onFocusIn: function(a) {
        1 == this.get_type() && null != this.stage && this.stage.get_focus() == this ? this.__startTextInput() : 1 != this.get_type() && this.get_selectable() && null != this.stage && this.stage.get_focus() == this && this.__startCursorTimer()
    },
    this_onFocusOut: function(a) {
        this.__stopCursorTimer();
        this.__stopTextInput();
        this.__selectionIndex != this.__caretIndex && (this.__selectionIndex = this.__caretIndex, this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()))
    },
    this_onKeyDown: function(a) {
        this.get_selectable() && 1 != this.get_type() && 67 == a.keyCode && (a.commandKey || a.ctrlKey) && (this.__caretIndex == this.__selectionIndex || this.get_displayAsPassword() || zc.set_text(this.__text.substring(this.__caretIndex, this.__selectionIndex)))
    },
    this_onMouseDown: function(a) {
        if (this.get_selectable() || 1 == this.get_type()) {
            this.__lineSelection =
                3 == a.clickCount;
            this.__wordSelection = 2 == a.clickCount;
            if (this.__lineSelection) {
                var b = this.__caretIndex;
                this.__caretIndex = this.__getPositionByIdentifier(a.stageX + this.get_scrollH(), a.stageY, !0);
                this.__selectionIndex = this.__getOppositeIdentifierBound(b, !0)
            } else this.__wordSelection ? (b = this.__caretIndex, this.__caretIndex = this.__getPositionByIdentifier(a.stageX + this.get_scrollH(), a.stageY, !1), this.__selectionIndex = this.__getOppositeIdentifierBound(b, !1), this.__specialSelectionInitialIndex = b) : this.__selectionIndex =
                this.__caretIndex = this.__getPosition(this.get_mouseX() + this.get_scrollH(), this.get_mouseY());
            this.setSelection(this.__caretIndex, this.__selectionIndex);
            this.__updateLayout();
            S.__supportDOM || (this.__dirty = !0, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
            null != this.stage && (this.stage.addEventListener("enterFrame", l(this, this.this_onEnterFrame)), this.stage.addEventListener("mouseMove", l(this, this.stage_onMouseMove)), this.stage.addEventListener("mouseUp", l(this, this.stage_onMouseUp)))
        }
    },
    this_onMouseWheel: function(a) {
        this.get_mouseWheelEnabled() && this.set_scrollV(Math.min(this.get_scrollV() - a.delta, this.get_maxScrollV()) | 0)
    },
    window_onKeyDown: function(a, b) {
        switch (a) {
            case 8:
                this.__selectionIndex == this.__caretIndex && 0 < this.__caretIndex && (this.__selectionIndex = this.__caretIndex - 1);
                this.__selectionIndex != this.__caretIndex ? (this.replaceSelectedText(""), this.__selectionIndex = this.__caretIndex, this.dispatchEvent(new wa("change", !0))) : (this.__stopCursorTimer(), this.__startCursorTimer());
                break;
            case 97:
                this.get_selectable() && (Xa.get_metaKey(b) || Xa.get_ctrlKey(b)) && this.setSelection(0, this.__text.length);
                break;
            case 99:
                if (Xa.get_metaKey(b) || Xa.get_ctrlKey(b)) this.__caretIndex == this.__selectionIndex || this.get_displayAsPassword() || zc.set_text(this.__text.substring(this.__caretIndex, this.__selectionIndex));
                break;
            case 120:
                !Xa.get_metaKey(b) && !Xa.get_ctrlKey(b) || this.__caretIndex == this.__selectionIndex || this.get_displayAsPassword() || (zc.set_text(this.__text.substring(this.__caretIndex, this.__selectionIndex)),
                    this.replaceSelectedText(""), this.dispatchEvent(new wa("change", !0)));
                break;
            case 127:
                this.__selectionIndex == this.__caretIndex && this.__caretIndex < this.__text.length && (this.__selectionIndex = this.__caretIndex + 1);
                this.__selectionIndex != this.__caretIndex ? (this.replaceSelectedText(""), this.__selectionIndex = this.__caretIndex, this.dispatchEvent(new wa("change", !0))) : (this.__stopCursorTimer(), this.__startCursorTimer());
                break;
            case 1073741898:
                this.get_selectable() && (Xa.get_metaKey(b) || Xa.get_ctrlKey(b) ? this.__caretIndex =
                    0 : this.__caretBeginningOfLine(), Xa.get_shiftKey(b) || (this.__selectionIndex = this.__caretIndex), this.setSelection(this.__selectionIndex, this.__caretIndex));
                break;
            case 1073741901:
                this.get_selectable() && (Xa.get_metaKey(b) || Xa.get_ctrlKey(b) ? this.__caretIndex = this.__text.length : this.__caretEndOfLine(), Xa.get_shiftKey(b) || (this.__selectionIndex = this.__caretIndex), this.setSelection(this.__selectionIndex, this.__caretIndex));
                break;
            case 1073741903:
                this.get_selectable() && (Xa.get_metaKey(b) || Xa.get_ctrlKey(b) ?
                    this.__caretBeginningOfNextLine() : this.__caretNextCharacter(), Xa.get_shiftKey(b) || (this.__selectionIndex = this.__caretIndex), this.setSelection(this.__selectionIndex, this.__caretIndex));
                break;
            case 1073741904:
                this.get_selectable() && (Xa.get_metaKey(b) || Xa.get_ctrlKey(b) ? this.__caretBeginningOfPreviousLine() : this.__caretPreviousCharacter(), Xa.get_shiftKey(b) || (this.__selectionIndex = this.__caretIndex), this.setSelection(this.__selectionIndex, this.__caretIndex));
                break;
            case 1073741905:
                this.get_selectable() &&
                    (Xa.get_metaKey(b) || Xa.get_ctrlKey(b) ? this.__caretIndex = this.__text.length : this.__caretNextLine(), Xa.get_shiftKey(b) || (this.__selectionIndex = this.__caretIndex), this.setSelection(this.__selectionIndex, this.__caretIndex));
                break;
            case 1073741906:
                this.get_selectable() && (Xa.get_metaKey(b) || Xa.get_ctrlKey(b) ? this.__caretIndex = 0 : this.__caretPreviousLine(), Xa.get_shiftKey(b) || (this.__selectionIndex = this.__caretIndex), this.setSelection(this.__selectionIndex, this.__caretIndex));
                break;
            case 13:
            case 1073741912:
                this.__textEngine.multiline ?
                    (a = new Ce("textInput", !0, !0, "\n"), this.dispatchEvent(a), a.isDefaultPrevented() || (this.__replaceSelectedText("\n", !0), this.dispatchEvent(new wa("change", !0)))) : (this.__stopCursorTimer(), this.__startCursorTimer())
        }
    },
    window_onTextInput: function(a) {
        this.__replaceSelectedText(a, !0);
        this.dispatchEvent(new wa("change", !0))
    },
    __class__: sc,
    __properties__: v(xa.prototype.__properties__, {
        set_wordWrap: "set_wordWrap",
        set_restrict: "set_restrict",
        set_borderColor: "set_borderColor",
        set_border: "set_border",
        set_backgroundColor: "set_backgroundColor",
        set_background: "set_background",
        set_autoSize: "set_autoSize",
        set_type: "set_type",
        get_type: "get_type",
        get_textWidth: "get_textWidth",
        get_textHeight: "get_textHeight",
        set_text: "set_text",
        get_text: "get_text",
        set_selectable: "set_selectable",
        get_selectable: "get_selectable",
        set_scrollV: "set_scrollV",
        get_scrollV: "get_scrollV",
        set_scrollH: "set_scrollH",
        get_scrollH: "get_scrollH",
        get_numLines: "get_numLines",
        get_multiline: "get_multiline",
        get_mouseWheelEnabled: "get_mouseWheelEnabled",
        get_maxScrollV: "get_maxScrollV",
        get_maxScrollH: "get_maxScrollH",
        get_length: "get_length",
        set_htmlText: "set_htmlText",
        get_htmlText: "get_htmlText",
        get_displayAsPassword: "get_displayAsPassword",
        set_defaultTextFormat: "set_defaultTextFormat",
        get_defaultTextFormat: "get_defaultTextFormat",
        get_caretIndex: "get_caretIndex",
        get_bottomScrollV: "get_bottomScrollV"
    })
});
var We = function(a, b, c, d, f, h, k) {
    sc.call(this);
    this.onSubmit = h;
    this.onCancel = k;
    this.set_defaultTextFormat(b);
    this.set_type(1);
    this.set_text(a);
    this.set_border(!0);
    this.set_borderColor(K.colorDark);
    this.set_background(!0);
    this.set_backgroundColor(K.colorPaper);
    this.set_autoSize(null != f ? f : 0);
    this.addEventListener("focusOut", l(this, this.onFocusOut));
    this.addEventListener("keyDown", l(this, this.onKeyDown));
    this.addEventListener("keyUp", l(this, this.onKeyUp));
    d.addChild(this);
    this.stage.set_focus(this);
    this.set_x(c.x - this.get_width() / 2);
    this.set_y(c.y - this.get_height() / 2)
};
g["com.watabou.mfcg.ui.EditInPlace"] = We;
We.__name__ = "com.watabou.mfcg.ui.EditInPlace";
We.fromTextField = function(a, b, c, d) {
    var f =
        a.get_defaultTextFormat(),
        h = a.get_scaleX();
    f.size = f.size * h | 0;
    h = b.globalToLocal(a.localToGlobal(new I(a.get_width() / 2 / h, a.get_height() / 2 / h)));
    a.set_visible(!1);
    return new We(a.get_text(), f, h, b, c, function(b) {
        a.set_visible(!0);
        d(b)
    }, function() {
        a.set_visible(!0)
    })
};
We.__super__ = sc;
We.prototype = v(sc.prototype, {
    onFocusOut: function(a) {
        this.submit()
    },
    onKeyDown: function(a) {
        switch (a.keyCode) {
            case 13:
                this.submit();
                break;
            case 27:
                this.cancel()
        }
        a.stopPropagation()
    },
    onKeyUp: function(a) {
        a.stopPropagation()
    },
    finish: function() {
        this.removeEventListener("focusOut",
            l(this, this.onFocusOut));
        this.removeEventListener("keyDown", l(this, this.onKeyDown));
        this.parent.removeChild(this)
    },
    submit: function() {
        this.finish();
        if (null != this.onSubmit) this.onSubmit(this.get_text())
    },
    cancel: function() {
        this.finish();
        if (null != this.onCancel) this.onCancel()
    },
    __class__: We
});
var Nd = function(a, b, c) {
    null == c && (c = !1);
    S.call(this);
    this.__drawableType = 2;
    this.__bitmapData = a;
    this.pixelSnapping = b;
    this.smoothing = c;
    null == b && (this.pixelSnapping = 1)
};
g["openfl.display.Bitmap"] = Nd;
Nd.__name__ = "openfl.display.Bitmap";
Nd.__super__ = S;
Nd.prototype = v(S.prototype, {
    __enterFrame: function(a) {
        null == this.__bitmapData || null == this.__bitmapData.image || this.__bitmapData.image.version == this.__imageVersion || this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty())
    },
    __getBounds: function(a, b) {
        var c = na.__pool.get();
        null != this.__bitmapData ? c.setTo(0, 0, this.__bitmapData.width, this.__bitmapData.height) : c.setTo(0, 0, 0, 0);
        c.__transform(c, b);
        a.__expand(c.x, c.y, c.width, c.height);
        na.__pool.release(c)
    },
    __hitTest: function(a,
        b, c, d, f, h) {
        if (!h.get_visible() || this.__isMask || null == this.__bitmapData || null != this.get_mask() && !this.get_mask().__hitTestMask(a, b)) return !1;
        this.__getRenderTransform();
        var k = this.__renderTransform,
            n = k.a * k.d - k.b * k.c;
        c = 0 == n ? -k.tx : 1 / n * (k.c * (k.ty - b) + k.d * (a - k.tx));
        k = this.__renderTransform;
        n = k.a * k.d - k.b * k.c;
        a = 0 == n ? -k.ty : 1 / n * (k.a * (b - k.ty) + k.b * (k.tx - a));
        if (0 < c && 0 < a && c <= this.__bitmapData.width && a <= this.__bitmapData.height) {
            if (null != this.__scrollRect && !this.__scrollRect.contains(c, a)) return !1;
            null == d || f || d.push(h);
            return !0
        }
        return !1
    },
    __hitTestMask: function(a, b) {
        if (null == this.__bitmapData) return !1;
        this.__getRenderTransform();
        var c = this.__renderTransform,
            d = c.a * c.d - c.b * c.c,
            f = 0 == d ? -c.tx : 1 / d * (c.c * (c.ty - b) + c.d * (a - c.tx));
        c = this.__renderTransform;
        d = c.a * c.d - c.b * c.c;
        a = 0 == d ? -c.ty : 1 / d * (c.a * (b - c.ty) + c.b * (c.tx - a));
        return 0 < f && 0 < a && f <= this.__bitmapData.width && a <= this.__bitmapData.height ? !0 : !1
    },
    get_bitmapData: function() {
        return this.__bitmapData
    },
    set_bitmapData: function(a) {
        this.__bitmapData = a;
        this.smoothing = !1;
        this.__renderDirty ||
            (this.__renderDirty = !0, this.__setParentRenderDirty());
        this.__imageVersion = -1;
        return this.__bitmapData
    },
    set_height: function(a) {
        null != this.__bitmapData ? this.set_scaleY(a / this.__bitmapData.height) : this.set_scaleY(0);
        return a
    },
    set_width: function(a) {
        null != this.__bitmapData ? this.set_scaleX(a / this.__bitmapData.width) : this.set_scaleX(0);
        return a
    },
    __class__: Nd,
    __properties__: v(S.prototype.__properties__, {
        set_bitmapData: "set_bitmapData",
        get_bitmapData: "get_bitmapData"
    })
});
var jh = function(a, b, c, d) {
    null == d &&
        (d = 4);
    this.matrix = new ua;
    this.obj = a;
    this.size = c;
    this.quality = d;
    this.color = new Tb;
    this.color.set_color(b);
    Nd.call(this);
    this.update(d)
};
g["com.watabou.mfcg.ui.Outline"] = jh;
jh.__name__ = "com.watabou.mfcg.ui.Outline";
jh.__super__ = Nd;
jh.prototype = v(Nd.prototype, {
    update: function(a) {
        null == a && (a = 0);
        null != this.get_bitmapData() && this.get_bitmapData().dispose();
        0 == a && (a = this.quality);
        var b = this.obj.getRect(this.obj),
            c = new Fb(Math.ceil(b.width * a), Math.ceil(b.height * a), !0, 0);
        this.matrix.identity();
        this.matrix.translate(-b.get_left(),
            -b.get_top());
        this.matrix.scale(a, a);
        c.draw(this.obj, this.matrix, null, null, null, !0);
        c.colorTransform(c.rect, this.color);
        for (var d = new Fb(Math.ceil((b.width + 2 * this.size) * a), Math.ceil((b.height + 2 * this.size) * a), !0, 0), f = Math.ceil(a * this.size * 6), h = 0; h < f;) {
            var k = h++;
            k = I.polar(this.size, k / f * 2 * Math.PI);
            k.offset(this.size, this.size);
            this.matrix.identity();
            this.matrix.translate(k.x * a, k.y * a);
            d.draw(c, this.matrix, null, null, null, !0)
        }
        c.dispose();
        this.set_bitmapData(d);
        this.smoothing = !0;
        this.set_scaleX(this.set_scaleY(1 /
            a));
        this.set_x(this.obj.get_x() + b.get_left() - this.size);
        this.set_y(this.obj.get_y() + b.get_top() - this.size)
    },
    __class__: jh
});
var vb = function() {};
g["com.watabou.mfcg.ui.Text"] = vb;
vb.__name__ = "com.watabou.mfcg.ui.Text";
vb.get = function(a, b) {
    var c = new sc;
    c.set_defaultTextFormat(b);
    c.set_autoSize(1);
    c.set_text(a);
    return c
};
vb.getFormat = function(a, b, c, d) {
    null == d && (d = 1);
    null != a && (b = ba.get(a, b));
    a = null != b.face ? b.face : ac.getFont(b.embedded).name;
    d = Math.round(b.size * d * vb.getMultiplier());
    return new we(a, d, c,
        b.bold, b.italic)
};
vb.getMultiplier = function() {
    switch (ba.get("text_size", 1)) {
        case 0:
            return .5;
        case 1:
            return .75;
        default:
            return 1
    }
};
var le = function() {
    this.isAwake = !1;
    this.awake = new ec;
    le.inst = this;
    ka.call(this);
    this.border = new Nd(new Fb(1, 1, !1, D.black));
    this.addChild(this.border);
    this.bg = new Nd(new Fb(1, 1, !1, D.white));
    this.bg.set_x(1);
    this.bg.set_y(1);
    this.addChild(this.bg);
    var a = D.black;
    null == a && (a = 0);
    this.tf = vb.get("", new we(ac.getFont("ui_font").name, 18, a));
    this.tf.set_x(6);
    this.tf.set_y(6);
    this.addChild(this.tf);
    cl.onActivate(this, l(this, this.activation));
    this.set(null)
};
g["com.watabou.mfcg.ui.Tooltip"] = le;
le.__name__ = "com.watabou.mfcg.ui.Tooltip";
le.__super__ = ka;
le.prototype = v(ka.prototype, {
    activation: function(a) {
        a ? (this.stage.addEventListener("mouseMove", l(this, this.onMouseMove)), this.stage.addEventListener("mouseDown", l(this, this.onMouseMove)), this.timer = rb.wait(5, l(this, this.fallAsleep))) : (this.stage.removeEventListener("mouseMove", l(this, this.onMouseMove)), this.stage.removeEventListener("mouseDown",
            l(this, this.onMouseMove)), null != this.timer && rb.cancel(this.timer))
    },
    onMouseMove: function(a) {
        this.set_x(this.parent.get_mouseX() + 16 / this.parent.get_scaleX());
        this.set_y(this.parent.get_mouseY());
        a.updateAfterEvent();
        this.wakeUp()
    },
    fallAsleep: function() {
        this.awake.dispatch(this.isAwake = !1);
        this.timer = null
    },
    wakeUp: function() {
        this.isAwake || this.awake.dispatch(this.isAwake = !0);
        null != this.timer && rb.cancel(this.timer);
        this.timer = rb.wait(5, l(this, this.fallAsleep))
    },
    set: function(a) {
        this.set_visible(null !=
            a);
        if (this.get_visible()) {
            this.tf.set_text(a);
            a = this.tf.get_width() + 12 | 0;
            var b = this.tf.get_height() + 12 | 0;
            this.border.set_width(a);
            this.border.set_height(b);
            this.bg.set_width(a - 2);
            this.bg.set_height(b - 2)
        }
    },
    __class__: le
});
var Nf = function(a) {
    var b = this;
    ic.call(this, ["OK", "Cancel"]);
    this.emblem = a;
    a = Nf.EDITOR;
    null != ra.coa && (a += "?coa=" + ra.coa);
    var c = new gb;
    c.setMargins(12, 10);
    c.add(new Ib('Open <a href="' + a + '"><b>Armoria editor</b></a> and design your emblem.<br/><i>Copy COA String</i> there and paste it here.'));
    this.input = new tc;
    this.input.enter.add(function(a) {
        b.onEnter()
    });
    this.input.set_prompt("COA");
    this.input.halign = "fill";
    c.add(this.input);
    this.add(c);
    null != ra.coa && this.input.set_text(ra.coa)
};
g["com.watabou.mfcg.ui.forms.EmblemForm"] = Nf;
Nf.__name__ = "com.watabou.mfcg.ui.forms.EmblemForm";
Nf.__super__ = ic;
Nf.prototype = v(ic.prototype, {
    getTitle: function() {
        return "Emblem"
    },
    onShow: function() {
        this.input.selecteAll();
        this.stage.set_focus(this.input)
    },
    onEnter: function() {
        this.onButton("OK")
    },
    onButton: function(a) {
        "OK" ==
        a && this.update();
        ic.prototype.onButton.call(this, a)
    },
    update: function() {
        ra.setCOA("" != this.input.get_text() ? this.input.get_text() : null)
    },
    __class__: Nf
});
var hh = function(a, b) {
    null == b && (b = !0);
    var c = this;
    ic.call(this, ["OK", b ? "Cancel" : "Delete"]);
    this.landmark = a;
    this.creating = b;
    b = new ed;
    var d = new Ib("Name");
    d.valign = "center";
    b.add(d);
    this.input = new tc(a.name);
    this.input.set_width(200);
    this.input.enter.add(function(a) {
        c.onButton("OK")
    });
    b.add(this.input);
    this.add(b)
};
g["com.watabou.mfcg.ui.forms.MarkerForm"] =
    hh;
hh.__name__ = "com.watabou.mfcg.ui.forms.MarkerForm";
hh.__super__ = ic;
hh.prototype = v(ic.prototype, {
    getTitle: function() {
        return "Landmark"
    },
    onShow: function() {
        this.stage.set_focus(this.input)
    },
    onButton: function(a) {
        var b = Ub.instance;
        switch (a) {
            case "Cancel":
                this.creating && b.removeLandmark(this.landmark);
                break;
            case "Delete":
                b.removeLandmark(this.landmark);
                break;
            case "OK":
                this.landmark.name = this.input.get_text(), Bb.landmarksChanged.dispatch()
        }
        this.dialog.hide()
    },
    __class__: hh
});
var pi = function(a) {
    U.call(this);
    var b = D.format(D.uiFont, D.smallSize, D.black);
    b.align = 0;
    this.tf = ld.get("", b);
    this.tf.set_x(-2);
    this.tf.set_y(-2);
    this.addChild(this.tf);
    this.update(a)
};
g["com.watabou.mfcg.ui.forms.TownInfo"] = pi;
pi.__name__ = "com.watabou.mfcg.ui.forms.TownInfo";
pi.__super__ = U;
pi.prototype = v(U.prototype, {
    update: function(a) {
        var b = a.countBuildings();
        a = a.bp.pop;
        0 == a && (a = 6 * b);
        var c = Math.pow(10, Math.floor(Math.log(a) / Math.log(10)) - 1);
        a = Math.ceil(a / c) * c | 0;
        this.tf.set_autoSize(1);
        this.tf.set_text("Number of buildings: " + b +
            "\nPopulation: ~" + a);
        b = this.tf.get_width();
        a = this.tf.get_height();
        this.tf.set_autoSize(2);
        this.tf.set_width(b);
        this.tf.set_height(Math.ceil(a));
        this.setSize(this.tf.get_width() - 4, this.tf.get_height() - 4 - 1)
    },
    __class__: pi
});
var Jf = function() {
    var a = this,
        b = ["Copy", "Generate"];
    b = ["Generate"];
    ic.call(this, b);
    b = new gb;
    var c = new gb;
    c.setMargins(0, 0);
    c.add(new Ib("Copy this URL to restore the current map later."));
    c.add(new Ib("Enter a stored URL here to restore a map."));
    b.add(c);
    this.input = new tc(za.getURL());
    this.input.enter.add(function(b) {
        a.onEnter()
    });
    this.input.set_prompt("URL");
    this.input.set_width(400);
    b.add(this.input);
    this.add(b);
    Bb.newModel.add(l(this, this.onNewModel))
};
g["com.watabou.mfcg.ui.forms.URLForm"] = Jf;
Jf.__name__ = "com.watabou.mfcg.ui.forms.URLForm";
Jf.__super__ = ic;
Jf.prototype = v(ic.prototype, {
    getTitle: function() {
        return "Permalink"
    },
    onShow: function() {
        this.highlight()
    },
    onHide: function() {
        Bb.newModel.remove(l(this, this.onNewModel))
    },
    onEnter: function() {
        this.onButton("Generate")
    },
    onButton: function(a) {
        switch (a) {
            case "Copy":
                Wk.write(za.getURL());
                q.show("URL was copied to clipboard");
                this.highlight();
                break;
            case "Generate":
                this.generate();
                this.highlight();
                break;
            default:
                ic.prototype.onButton.call(this, a)
        }
    },
    onNewModel: function(a) {
        this.input.set_text(za.getURL());
        this.highlight()
    },
    generate: function() {
        var a = this.input.get_text();
        za.fromString(a);
        new Ub(Fd.fromURL());
        bb.switchScene(Ec);
        a = u.findForm(Kd);
        null != a && a.update()
    },
    highlight: function() {
        this.stage.set_focus(this.input);
        this.input.selecteAll()
    },
    __class__: Jf
});
var ji = function(a, b, c) {
    null ==
        c && (c = 10);
    this.cuts = [];
    this.minTurnOffset = 1;
    this.poly = a;
    this.minArea = b;
    this.variance = c;
    this.minOffset = Math.sqrt(b);
    this.processCut = l(this, this.detectStraight);
    this.isAtomic = l(this, this.isSmallEnough);
    this.shape = a
};
g["com.watabou.mfcg.utils.Bisector"] = ji;
ji.__name__ = "com.watabou.mfcg.utils.Bisector";
ji.prototype = {
    partition: function() {
        return this.subdivide(this.shape)
    },
    subdivide: function(a) {
        if (this.isAtomic(a)) return [a];
        var b = this.makeCut(a);
        if (1 == b.length) return [a];
        a = [];
        for (var c = 0; c < b.length;) {
            var d =
                b[c];
            ++c;
            d = this.subdivide(d);
            for (var f = 0; f < d.length;) {
                var h = d[f];
                ++f;
                a.push(h)
            }
        }
        return a
    },
    isSmallEnough: function(a) {
        var b = this.minArea * Math.pow(this.variance, Math.abs(((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 2 - 1));
        return Sa.area(a) < b
    },
    makeCut: function(a, b) {
        null == b && (b = 0);
        if (10 < b) return [a];
        var c = a.length;
        if (0 < b) {
            var d = I.polar(1, b / 10 * Math.PI * 2);
            var f = Yc.rotateYX(a,
                d.y, d.x);
            f = Yc.rotateYX(Gb.aabb(f), -d.y, d.x)
        } else f = Gb.obb(a);
        d = f[0];
        var h = f[1].subtract(d),
            k = f[3].subtract(d);
        if (h.get_length() < k.get_length()) {
            var n = h;
            h = k;
            k = n
        }
        f = Sa.centroid(a);
        f = wd.project(h, f.subtract(d));
        f = (f + ((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3) / 2;
        var p = new I(d.x + h.x * f, d.y + h.y * f);
        d = -1;
        for (var g = f = null, q = 0, m = 0, u = c; m < u;) {
            var r = m++,
                l = a[r];
            n = a[(r + 1) % c];
            var x = n.subtract(l);
            if (!(1E-10 > x.get_length()) &&
                (n = qa.intersectLines(p.x, p.y, k.x, k.y, l.x, l.y, x.x, x.y), null != n && 0 < n.y && 1 > n.y)) {
                var D = x;
                D = D.clone();
                D.normalize(1);
                var w = D;
                D = Math.abs(h.x * w.x + h.y * w.y);
                q < D && (q = D, d = r, D = n.y, f = new I(l.x + x.x * D, l.y + x.y * D), g = w)
            }
        }
        g.setTo(-g.y, g.x);
        h = Infinity;
        p = null;
        k = -1;
        m = 0;
        for (u = c; m < u;) r = m++, r != d && (l = a[r], n = a[(r + 1) % c], x = n.subtract(l), 1E-10 > x.get_length() || (n = qa.intersectLines(f.x, f.y, g.x, g.y, l.x, l.y, x.x, x.y), null != n && 0 < n.x && n.x < h && 0 < n.y && 1 > n.y && (h = n.x, p = x, k = r)));
        if (-1 == k) throw X.thrown("CRITICAL: A bad poly was provided for besecting");
        m = g.x * p.y - g.y * p.x;
        D = m * m / (g.x * g.x + g.y * g.y) / (p.x * p.x + p.y * p.y);
        if (.99 < D && (m = new I(f.x + g.x * h, f.y + g.y * h), l = [f, m], r = this.split(a.slice(), d, k, l), m = Sa.area(r[0]), u = Sa.area(r[1]), Math.max(m / u, u / m) < 2 * this.variance)) {
            this.cuts.push(l);
            if (null != this.getGap) {
                a = Qd.stripe(l, this.getGap(l));
                m = [];
                for (u = 0; u < r.length;) b = r[u], ++u, c = ye.and(b, Z.revert(a), !0), m.push(null != c ? c : b);
                r = m
            }
            return r
        }
        m = this.minOffset / h;
        m = .5 < m ? .5 : m + (1 - 2 * m) * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 +
            (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3);
        n = h * m;
        p = new I(f.x + g.x * n, f.y + g.y * n);
        k = -1;
        q = null;
        h = -Infinity;
        m = 0;
        for (u = c; m < u;)
            if (r = m++, r != d && (l = a[r], n = a[(r + 1) % c], x = n.subtract(l), D = x.get_length(), !(1E-10 > D) && (n = qa.intersectLines(p.x, p.y, x.y, -x.x, l.x, l.y, x.x, x.y), 0 < n.x && 0 < n.y && 1 > n.y && (w = D = (g.x * x.y - g.y * x.x) / D, h < w)))) {
                for (var z = !0, J = 0, v = c; J < v;) {
                    var y = J++;
                    if (y != r && y != d && (D = a[y], y = a[(y + 1) % c].subtract(D), !(1E-10 > y.get_length()) && (D = qa.intersectLines(p.x, p.y, x.y, -x.x, D.x, D.y, y.x, y.y), null != D && 0 <= D.x && 1 >=
                            D.x && 0 <= D.y && 1 >= D.y))) {
                        z = !1;
                        break
                    }
                }
                z && (h = w, k = r, r = n.y, q = new I(l.x + x.x * r, l.y + x.y * r))
            } if (null != q) {
            c = [f, p, q];
            l = this.processCut(c);
            m = 1;
            for (u = l.length - 2; m < u;)
                if (r = m++, !Gb.containsPoint(a, l[r])) {
                    l = c;
                    break
                } r = this.split(a.slice(), d, k, l);
            m = Sa.area(r[0]);
            u = Sa.area(r[1]);
            if (Math.max(m / u, u / m) > 2 * this.variance) return this.makeCut(a, b + 1);
            this.cuts.push(l);
            if (null != this.getGap) {
                a = Qd.stripe(l, this.getGap(l));
                m = [];
                for (u = 0; u < r.length;) b = r[u], ++u, c = ye.and(b, Z.revert(a), !0), m.push(null != c ? c : b);
                r = m
            }
            return r
        }
        hb.trace("(" +
            b + ") Failed to make a cut", {
                fileName: "Source/com/watabou/mfcg/utils/Bisector.hx",
                lineNumber: 281,
                className: "com.watabou.mfcg.utils.Bisector",
                methodName: "makeCut"
            });
        return this.makeCut(a, b + 1)
    },
    split: function(a, b, c, d) {
        var f = a[c],
            h = d[0];
        a[b] != h && (b < c && ++c, a.splice(++b, 0, h));
        h = d[d.length - 1];
        f != h && (c < b && ++b, a.splice(++c, 0, h));
        if (b < c) {
            f = a.slice(b + 1, c);
            h = Z.revert(d);
            for (var k = 0; k < h.length;) {
                var n = h[k];
                ++k;
                f.push(n)
            }
            c = a.slice(c + 1);
            h = a.slice(0, b);
            for (k = 0; k < h.length;) n = h[k], ++k, c.push(n)
        } else {
            f = a.slice(b + 1);
            h = a.slice(0, c);
            for (k = 0; k < h.length;) n = h[k], ++k, f.push(n);
            h = Z.revert(d);
            for (k = 0; k < h.length;) n = h[k], ++k, f.push(n);
            c = a.slice(c + 1, b)
        }
        for (k = 0; k < d.length;) n = d[k], ++k, c.push(n);
        return [f, c]
    },
    detectStraight: function(a) {
        if (0 < this.minTurnOffset) {
            var b = a[0],
                c = a[2];
            return Math.abs(Sa.area(a)) / I.distance(b, c) < this.minTurnOffset ? [b, c] : a
        }
        return a
    },
    __class__: ji
};
var mf = function() {};
g["com.watabou.mfcg.utils.Bloater"] = mf;
mf.__name__ = "com.watabou.mfcg.utils.Bloater";
mf.bloat = function(a, b) {
    for (var c = a.length, d = [], f = 0; f <
        c;) {
        var h = f++;
        h = mf.extrudeEx(a[h], a[(h + 1) % c], b);
        for (var k = 0; k < h.length;) {
            var n = h[k];
            ++k;
            d.push(n)
        }
    }
    return d
};
mf.extrude = function(a, b, c) {
    var d = a.subtract(b);
    c = d.get_length() / c;
    return .3 < c ? (d.setTo(-d.y, d.x), c = .5 * (1 > c ? c : 1), d.x *= c, d.y *= c, a = qa.lerp(a, b), a.x += d.x, a.y += d.y, a) : null
};
mf.extrudeEx = function(a, b, c) {
    var d = mf.extrude(a, b, c);
    return null == d ? [a] : mf.extrudeEx(a, d, c).concat(mf.extrudeEx(d, b, c))
};
var Zk = function() {};
g["com.watabou.mfcg.utils.Cutter"] = Zk;
Zk.__name__ = "com.watabou.mfcg.utils.Cutter";
Zk.grid =
    function(a, b, c, d) {
        null == d && (d = 0);
        if (4 != a.length) throw new Vb("Not a quadrangle!");
        for (var f = [], h = 0, k = b + 1; h < k;) {
            var n = h++;
            f.push(n / b)
        }
        var p = f;
        f = [];
        h = 0;
        for (k = c + 1; h < k;) n = h++, f.push(n / c);
        var g = f;
        if (0 < d) {
            f = 1;
            for (h = b; f < h;) n = f++, p[n] += (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 - .5) / (b - 1) * d;
            f = 1;
            for (h = c; f < h;) n = f++, g[n] += (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 +
                (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 - .5) / (c - 1) * d
        }
        var q = a[0],
            m = a[1],
            u = a[2],
            r = a[3];
        f = [];
        h = 0;
        for (k = c + 1; h < k;) {
            n = h++;
            var l = qa.lerp(q, r, g[n]);
            n = qa.lerp(m, u, g[n]);
            a = [];
            for (var x = 0, D = b + 1; x < D;) d = x++, a.push(qa.lerp(l, n, p[d]));
            f.push(a)
        }
        p = f;
        g = [];
        f = 0;
        for (h = c; f < h;)
            for (n = f++, k = 0, a = b; k < a;) d = k++, g.push([p[n][d], p[n][d + 1], p[n + 1][d + 1], p[n + 1][d]]);
        return g
    };
var gk = function() {};
g["com.watabou.mfcg.utils.GraphicsUtils"] = gk;
gk.__name__ = "com.watabou.mfcg.utils.GraphicsUtils";
gk.getMaxArea = function(a, b) {
    null ==
        b && (b = !0);
    var c = 0,
        d = null;
    a instanceof ka ? d = va.__cast(a, ka).get_graphics().__bounds : a instanceof md && (d = va.__cast(a, md).get_graphics().__bounds);
    null != d && (c = d.width * d.height);
    if (b && a instanceof kb)
        for (a = va.__cast(a, kb), b = 0, d = a.get_numChildren(); b < d;) {
            var f = b++;
            f = a.getChildAt(f);
            c = Math.max(c, gk.getMaxArea(f))
        }
    return c
};
var yi = function(a) {
    this.path = a;
    this.size = a.length;
    this.reset()
};
g["com.watabou.mfcg.utils.PathTracker"] = yi;
yi.__name__ = "com.watabou.mfcg.utils.PathTracker";
yi.prototype = {
    getPos: function(a) {
        for (a <
            this.offset && this.reset(); a > this.offset + this.curLength;) {
            if (++this.curIndex >= this.size - 1) return this.reset(), null;
            this.offset += this.curLength;
            this.curVector = this.path[this.curIndex + 1].subtract(this.path[this.curIndex]);
            this.curLength = this.curVector.get_length()
        }
        return qa.lerp(this.path[this.curIndex], this.path[this.curIndex + 1], (a - this.offset) / this.curLength)
    },
    getSegment: function(a, b) {
        a = this.getPos(a);
        var c = this.curIndex + 1;
        b = this.getPos(b);
        c = this.path.slice(c, this.curIndex + 1);
        c.unshift(a);
        c.push(b);
        return c
    },
    reset: function() {
        this.curIndex = this.offset = 0;
        this.curVector = this.path[1].subtract(this.path[0]);
        this.curLength = this.curVector.get_length()
    },
    length: function() {
        return Sa.$length(this.path)
    },
    get_tangent: function() {
        return this.curVector
    },
    __class__: yi,
    __properties__: {
        get_tangent: "get_tangent"
    }
};
var uc = function() {};
g["com.watabou.mfcg.utils.PolyUtils"] = uc;
uc.__name__ = "com.watabou.mfcg.utils.PolyUtils";
uc.lerpVertex = function(a, b) {
    b = a.indexOf(b);
    var c = a.length;
    return qa.lerp(a[(b + c - 1) % c], a[(b + 1) %
        c])
};
uc.smooth = function(a, b, c) {
    null == c && (c = 1);
    for (var d = a.length, f = 0; f < c;) {
        f++;
        for (var h = [], k = 0, n = d; k < n;) {
            var p = k++,
                g = a[p];
            null != b && -1 != b.indexOf(g) ? h.push(g) : h.push(qa.lerp(qa.lerp(a[(p + d - 1) % d], a[(p + 1) % d]), g))
        }
        a = h
    }
    return a
};
uc.smoothOpen = function(a, b, c) {
    null == c && (c = 1);
    for (var d = a.length, f = 0; f < c;) {
        f++;
        for (var h = [], k = 0, n = d; k < n;) {
            var p = k++,
                g = a[p];
            0 == p || p == d - 1 || null != b && -1 != b.indexOf(g) ? h.push(g) : h.push(qa.lerp(qa.lerp(a[p - 1], a[p + 1]), g))
        }
        a = h
    }
    return a
};
uc.simpleInset = function(a, b) {
    for (var c = a.length,
            d = [], f = 0; f < c;) {
        var h = f++,
            k = a[h],
            n = a[(h + 1) % c],
            p = b[(h + c - 1) % c],
            g = b[h],
            q = k.subtract(a[(h + c - 1) % c]);
        h = q.get_length();
        n = n.subtract(k);
        var m = n.get_length(),
            u = q.x * n.y - q.y * n.x;
        g = -g / (u / m);
        k = new I(k.x + q.x * g, k.y + q.y * g);
        p /= u / h;
        d.push(new I(k.x + n.x * p, k.y + n.y * p))
    }
    return d
};
uc.inset = function(a, b) {
    for (var c = a, d = a.length, f = 0, h = 0; h < d;) {
        var k = h++;
        if (b[k] != b[(k + d - 1) % d]) {
            f = k;
            break
        }
    }
    h = f;
    k = [a[h]];
    for (var n = b[h];;) {
        do h = (h + 1) % d, k.push(a[h]); while (h != f && b[h] == n);
        if (0 != n)
            if (k[0] != k[k.length - 1]) k = Qd.stripe(k, 2 * n), k = ye.and(c,
                Z.revert(k), !0), null != k && (c = k);
            else {
                var p = Qd.stripe([k[0], k[1]], 2 * n);
                p = ye.and(c, Z.revert(p), !0);
                null != p && (c = p);
                k = Qd.stripe(k.slice(1), 2 * n);
                k = ye.and(c, Z.revert(k), !0);
                null != k && (c = k)
