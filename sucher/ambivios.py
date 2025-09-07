import libjevois as jevois
import cv2
import numpy as np
import math

## Detect colours on a TV and send this information over serial
#
# @author Markus Mohr
#
# @videomapping YUYV 640 480 10 YUYV 640 480 10 Me AmbiVois
# @email markus@johalla.de
# @copyright Copyright (C) 2018 by Markus Mohr
# @mainurl -
# @supporturl -
# @otherurl -
# @license GPL v3
# @distribution Unrestricted
# @restrictions None
# @ingroup modules
class AmbiVoisLowRes:
    # ###################################################################################################
    # define corners
    topleft = (77,29)
    topright =(248,79)
    botleft = (76, 200)
    botright = (255,185)

    # number of windows (no corners for ywindows!)
    xwindows = 20
    ywindows = 16
   
    # depth of a windows towards the center
    depth = 30
   
    # calculate width (for x) and heigth (for y) of windows
    xtopwinwidth = (topright[0]-topleft[0])/xwindows
    xbotwinwidth = (botright[0]-botleft[0])/xwindows
    yleftwinheight = (botleft[1]-topleft[1])/ywindows
    yrightwinheight = (botright[1]-topright[1])/ywindows
   
    # calculate slopes to get the window positions right
    topslope = (topright[1]-topleft[1])/xwindows
    bottomslope = (botright[1]-botleft[1])/xwindows
    leftslope = (botleft[0]-topleft[0])/ywindows
    rightslope = (botright[0]-topright[0])/ywindows
   
   
       
    ## Constructor
    def __init__(self):
        # Instantiate a JeVois Timer to measure our processing framerate:
        self.timer = jevois.Timer("processing timer", 100, jevois.LOG_INFO)
       
    def mean_bgr(self,img,x1,y1,x2,y2):
        """calculates the square mean of each color in the area x1,y1 - x2,y2"""
        blue = green = red = 0
        step = 2
        pixelcount = (x2-x1)*(y2-y1)
        for j in range(x1,x2,step):
            for k in range(y1,y2,2):      # sum the squares of each colour
                blue += img.item(k,j,0) ** 2
                green += img.item(k, j, 1) ** 2
                red += img.item(k, j, 2) ** 2
        blue = round(math.sqrt(step**2 *blue/pixelcount))    # square root of average colour (squared)
        green = round(math.sqrt(step**2 *green/pixelcount))
        red = round(math.sqrt(step**2 *red/pixelcount))
        return(blue,green,red)

    def draw_cross(img,x,y,l,t):
        cv2.line(img,(x+2*t,y),(x+2*t+l,y),(0,255,0),t)
        cv2.line(img,(x-2*t,y),(x-2*t-l,y),(0,255,0),t)
        cv2.line(img,(x,y+2*t),(x,y+2*t+l),(0,255,0),t)
        cv2.line(img,(x,y-2*t),(x,y-2*t-l),(0,255,0),t)
                               
    # ###################################################################################################
    ## Process function with USB output
    def process(self, inframe, outframe):
       


        img = inframe.getCvBGR()
        height = img.shape[0]
        width = img.shape[1]
       
        # Start measuring image processing time (NOTE: does not account for input conversion time):
        self.timer.start()

        for i in range(0, self.xwindows):
            # top
            x1 = self.topleft[0] + round(i * self.xtopwinwidth)
            y1 = self.topleft[1] + round(i * self.topslope)
            x2 = x1 + round(self.xtopwinwidth)
            y2 = y1 + self.depth
            mean_colour = self.mean_bgr(img,x1,y1,x2,y2)
            #img = cv2.rectangle(img,(x1,y1), (x2,y2), (0, 255, 0), 1)
            img = cv2.rectangle(img,(x1,y1-self.depth), (x2,y1), mean_colour, -1)    # test
            #bottom
            x1 = self.botleft[0] + round(i * self.xbotwinwidth)
            y1 = self.botleft[1] + round(i * self.bottomslope) - self.depth
            x2 = x1 + round(self.xbotwinwidth)
            y2 = y1 + self.depth
            mean_colour = self.mean_bgr(img,x1,y1,x2,y2)
            #img = cv2.rectangle(img,(x1,y1), (x2,y2), (0, 255, 0), 1)
            img = cv2.rectangle(img,(x1,y1+self.depth), (x2,y2+self.depth), mean_colour, -1)    # test
       
        for i in range(0, self.ywindows):  # edges already calculated above
            # left
            x1 = self.topleft[0] + round(i * self.leftslope)
            y1 = self.topleft[1] + round(i * self.yleftwinheight)
            x2 = x1 + self.depth
            y2 = y1 + round(self.yleftwinheight)
            mean_colour = self.mean_bgr(img,x1,y1,x2,y2)
            #img = cv2.rectangle(img,(x1,y1), (x2,y2), (255, 0, 0), 1)
            img = cv2.rectangle(img,(x1-self.depth,y1), (x1,y2), mean_colour, -1)    # average colours
            # right
            x1 = self.topright[0] + round(i * self.rightslope) - self.depth
            y1 = self.topright[1] + round(i * self.yrightwinheight)
            x2 = x1 + self.depth
            y2 = y1 + round(self.yrightwinheight)
            mean_colour = self.mean_bgr(img,x1,y1,x2,y2)
            #img = cv2.rectangle(img,(x1,y1), (x2,y2), (255, 0, 0), 1)
            img = cv2.rectangle(img,(x2,y1), (x2+self.depth,y2), mean_colour, -1)    # test

       
        # Write a title:
        cv2.putText(img, "JeVois AmbiVois", (3, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255,255,255))
       
        # Write frames/s info from our timer into the edge map (NOTE: does not account for output conversion time):
        fps = self.timer.stop()

        cv2.putText(img, fps, (3, height - 6), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255,255,255))
       
        jevois.LINFO('fps: ' + str(fps))
        # Convert our output image to video output format and send to host over USB:
        outframe.sendCv(img)