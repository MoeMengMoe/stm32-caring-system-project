// Caring Node V0.5 single-board fixture concept.
// Units: mm.
// This is a planning model, not final STL. Replace module placeholders after measuring real parts.

$fn = 48;

base_x = 240;
base_y = 180;
base_z = 1;
ins_z = 2;

module_z = 3;

module_color = [0.55, 0.75, 1.0, 0.65];
raised_color = [0.55, 1.0, 0.55, 0.65];
sensor_color = [1.0, 0.72, 0.35, 0.75];
danger_color = [1.0, 0.45, 0.45, 0.65];

module_height = base_z + ins_z;

module footprint(label, x, y, sx, sy, z, h, color_v) {
  color(color_v)
    translate([x, y, z])
      cube([sx, sy, h]);
}

module plate() {
  color([0.55, 0.55, 0.55, 0.45])
    cube([base_x, base_y, base_z]);
  color([1.0, 0.95, 0.68, 0.85])
    translate([4, 4, base_z])
      cube([base_x - 8, base_y - 8, ins_z]);
}

module sensor_mast() {
  // Front mast: non-metal vertical plate carrying Rd-03 and PIR.
  color([0.55, 1.0, 0.55, 0.85])
    translate([78, 2, module_height])
      cube([84, 4, 90]);
  // Rd-03 placeholder, centered lower.
  color(sensor_color)
    translate([95, -1, module_height + 35])
      cube([20, 3, 20]);
  // PIR placeholder, higher and wider.
  color(sensor_color)
    translate([124, -1, module_height + 48])
      cube([32, 3, 24]);
  color([1, 1, 1, 0.8])
    translate([140, -3, module_height + 60])
      rotate([90, 0, 0])
        cylinder(h = 8, r = 11);
}

module tft_stand() {
  // Simple tilted TFT panel placeholder.
  color(raised_color)
    translate([98, 54, module_height + 15])
      rotate([25, 0, 0])
        cube([82, 58, 3]);
  color([0.25, 0.25, 0.25, 0.7])
    translate([102, 58, module_height + 18])
      rotate([25, 0, 0])
        cube([74, 46, 1]);
}

module button(x, y, color_v) {
  color(color_v)
    translate([x, y, module_height])
      cylinder(h = 8, r = 10);
}

plate();

// Coordinate convention in this SCAD:
// x: left to right, y: front to rear, front edge y=0.
sensor_mast();

footprint("ESP8266", 8, 8, 34, 23, module_height, module_z, raised_color);
footprint("5V power", 46, 8, 45, 22, module_height, module_z, danger_color);
footprint("BME280", 8, 30, 24, 14, module_height + 8, module_z, raised_color);
footprint("MQ", 193, 17, 35, 35, module_height + 8, module_z, sensor_color);
footprint("NUCLEO", 8, 50, 78, 120, module_height, module_z, module_color);
footprint("mini breadboard", 91, 38, 50, 15, module_height, module_z, module_color);
tft_stand();
button(108, 114, danger_color);
button(140, 114, raised_color);
footprint("buzzer", 164, 104, 22, 22, module_height + 5, module_z, raised_color);
footprint("relay", 98, 140, 124, 36, module_height, module_z, danger_color);
