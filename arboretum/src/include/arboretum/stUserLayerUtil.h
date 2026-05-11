/**
* @file
*
* This file defines a set of classes for user layer developers.
*
* @version 1.0
* @author Fabio Jun Takada Chino (chino@icmc.usp.br)
* @author Marcos Rodrigues Vieira (mrvieira@icmc.usp.br)
*/
#ifndef __STUSERLAYERUTIL_H
#define __STUSERLAYERUTIL_H

#include <stdexcept>
#include <cstdint>
//#include <arboretum/stTypes.h>

/**
* This class may be used to add a distance counter to any metric evaluator.
* To use this class, the metric evaluator must extend this class and call
* UpdateDistanceCount() to update the counter every time GetDistance()
* (or GetDistance2()) is called.
*
* <p>These methods are not required for the standard metric evaluator classes.
*
* @ingroup userlayerutil
* @author Fabio Jun Takada Chino (chino@icmc.usp.br)
* @author Marcos Rodrigues Vieira (mrvieira@icmc.usp.br)
* @todo This class is undocumented.
*/
class stMetricEvaluatorStatistics{
   public:
      stMetricEvaluatorStatistics(){ diint64_t = 0; };

      stMetricEvaluatorStatistics(int64_t d){ diint64_t = d; };

      stMetricEvaluatorStatistics(const stMetricEvaluatorStatistics& evaluator){ diint64_t = evaluator.diint64_t; };

      stMetricEvaluatorStatistics& operator=(const stMetricEvaluatorStatistics& evaluator){
         diint64_t = evaluator.diint64_t;
         return *this;
      };

   	  ~stMetricEvaluatorStatistics(){};

      /**
      * Resets statistics.
      */
      void ResetStatistics(){
         diint64_t = 0;
      }//end ResetStatistics

      /**
      * Returns the number of distances performed.
      */
      int64_t GetDistanceCount() {
         return diint64_t;
      }//end GetDistanceCount

      /**
      * Updates the distance counter by adding 1.
      */
      void UpdateDistanceCount(){
         diint64_t++;
      }//end UpdateDistanceCount

   protected:
      /**
      * The distance counter itself.
      */
      int64_t diint64_t;
};//end stMetricEvaluatorStatistics

#endif //__STUSERLAYERUTIL_H
