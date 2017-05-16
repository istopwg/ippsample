/*
 * A 3D interpretation of the PWG IPP logo, with keychain grommet added.
 *
 * Copyright © 2016 by Michael R Sweet.
 */

/* Parameters */
$finalwidth = 1.25 * 25.4;
$scaling    = $finalwidth / 20.0;
$bumpout    = 0.75 / $scaling;

scale(v=[$scaling,$scaling,$scaling]) {
    /* COLOR: BLACK */
    color([0.25,0.25,0.25]) {
        
        /* Keyring grommet */
        translate(v=[-5,13.75,0]) {
            difference() {
                cylinder(h=2, r=2.75, $fn=100);
                translate(v=[0,0,-1]) {
                    cylinder(h=4, r=1.75, $fn=100);
                }
            }   
        }
        /* Scanner head */
        linear_extrude(height=3) {
            polygon(points=[[-10,8],[-10,11],[-9-cos(22.5),11+sin(22.5)],[-9-cos(45),11+sin(45)],[-9-cos(67.5),11+sin(67.5)],[-9,12],[-2,12],[-1,10],[4,11],[10,10],[0,8]]);
        }
        /* Base for "3D" */
        linear_extrude(height=2) {
            polygon(points=[[-10,-16],[-10,-10],[10,-10],[10,-16],[9+cos(22.5),-16-sin(22.5)],[9+cos(45),-16-sin(45)],[9+cos(67.5),-16-sin(67.5)],[9,-17],[-9,-17],[-9-cos(67.5),-16-sin(67.5)],[-9-cos(45),-16-sin(45)],[-9-cos(22.5),-16-sin(22.5)]]);
        }
        /* Small wedge for "3D" */
        translate(v=[0,-10,2]) {
            rotate(a=[90,0,0]) {
                linear_extrude(height=7) {
                    polygon(points=[[-5,0],[0,1.25],[5,0]]);
                }
            }
        }
    }
    
    /* COLOR: PWG BLUE */
    color([75/255,90/255,168/255]) {
        /* Center body */
        translate(v=[0,8,0]) {
            rotate(a=[90,0,0]) {
                linear_extrude(height=18) {
                    polygon(points=[[-10,0],[-10,3],[-9-cos(22.5),3+sin(22.5)],[-9-cos(45),3+sin(45)],[-9-cos(67.5),3+sin(67.5)],[-9,4],[9,4],[9+cos(67.5),3+sin(67.5)],[9+cos(45),3+sin(45)],[9+cos(22.5),3+sin(22.5)],[10,3],[10,0]]);
                }
            }
        }
    }
    
    /* COLOR: WHITE */
    color([1,1,1]) {
        /* Activity light */
        translate(v=[0,0,4]) {
            linear_extrude(height=$bumpout) {
                polygon(points=[[-8,5.5],[-8,6.5],[-5,6.5],[-5,5.5]]);
            }
        }
        /* Top line */
        translate(v=[0,4,0]) {
            rotate(a=[90,0,0]) {
                linear_extrude(height=0.5) {
                    $bottom = 3;
                    $top    = $bottom + $bumpout;
                    polygon(points=[[-10,$bottom],[-10,$top],[-9-cos(22.5),$top+sin(22.5)],[-9-cos(45),$top+sin(45)],[-9-cos(67.5),$top+sin(67.5)],[-9,$top+1],[9,$top+1],[9+cos(67.5),$top+sin(67.5)],[9+cos(45),$top+sin(45)],[9+cos(22.5),$top+sin(22.5)],[10,$top],[10,$bottom],[9+cos(22.5),$bottom+sin(22.5)],[9+cos(45),$bottom+sin(45)],[9+cos(67.5),$bottom+sin(67.5)],[9,$bottom+1],[-9,$bottom+1],[-9-cos(67.5),$bottom+sin(67.5)],[-9-cos(45),$bottom+sin(45)],[-9-cos(22.5),$bottom+sin(22.5)]]);
                }
            }
        }
        /* Bottom line */
        translate(v=[0,-6,0]) {
            rotate(a=[90,0,0]) {
                linear_extrude(height=0.5) {
                    $bottom = 3;
                    $top    = $bottom + $bumpout;
                    polygon(points=[[-10,$bottom],[-10,$top],[-9-cos(22.5),$top+sin(22.5)],[-9-cos(45),$top+sin(45)],[-9-cos(67.5),$top+sin(67.5)],[-9,$top+1],[9,$top+1],[9+cos(67.5),$top+sin(67.5)],[9+cos(45),$top+sin(45)],[9+cos(22.5),$top+sin(22.5)],[10,$top],[10,$bottom],[9+cos(22.5),$bottom+sin(22.5)],[9+cos(45),$bottom+sin(45)],[9+cos(67.5),$bottom+sin(67.5)],[9,$bottom+1],[-9,$bottom+1],[-9-cos(67.5),$bottom+sin(67.5)],[-9-cos(45),$bottom+sin(45)],[-9-cos(22.5),$bottom+sin(22.5)]]);
                }
            }
        }
        /* "IPP" */
        translate(v=[0,-1.35,4]) {
            linear_extrude(height=$bumpout) {
                text("IPP", font="Helvetica Neue:style=Bold", size=6.5, halign="center", valign="center");
            }
        }
        /* "3D" */
        translate(v=[0,-13.5,3.25]) {
            rotate(a=[0,-14,0]) {
                linear_extrude(height=$bumpout) {
                    text("3", font="Helvetica Neue:style=Bold+Italic", size=5, halign="right", valign="center");
                }
            }
        }
        translate(v=[0,-13.5,3.25]) {
            rotate(a=[0,14,0]) {
                linear_extrude(height=$bumpout) {
                    text("D", font="Helvetica Neue:style=Bold+Italic", size=5, halign="left", valign="center");
                }
            }
        }
    }
}
