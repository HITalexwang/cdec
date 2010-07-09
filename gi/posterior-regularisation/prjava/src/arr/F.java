package arr;

import java.util.Random;

public class F {
	public static Random rng = new Random();

	public static void randomise(double probs[])
	{
		randomise(probs, true);
	}

	public static void randomise(double probs[], boolean normalise)
	{
		double z = 0;
		for (int i = 0; i < probs.length; ++i)
		{
			probs[i] = 3 + rng.nextDouble();
			if (normalise)
				z += probs[i];
		}

		if (normalise)
			for (int i = 0; i < probs.length; ++i)
				probs[i] /= z;
	}
	
	public static void l1normalize(double [] a){
		double sum=0;
		for(int i=0;i<a.length;i++){
			sum+=a[i];
		}
		if(sum==0){
			return ;
		}
		for(int i=0;i<a.length;i++){
			a[i]/=sum;
		}
	}
	
	public  static void l1normalize(double [][] a){
		double sum=0;
		for(int i=0;i<a.length;i++){
			for(int j=0;j<a[i].length;j++){
				sum+=a[i][j];
			}
		}
		if(sum==0){
			return;
		}
		for(int i=0;i<a.length;i++){
			for(int j=0;j<a[i].length;j++){
				a[i][j]/=sum;
			}
		}
	}
	
	public static double l1norm(double a[]){
		double norm=0;
		for(int i=0;i<a.length;i++){
			norm += a[i];
		}
		return norm;
	}
	
	public static int argmax(double probs[])
	{
		double m = Double.NEGATIVE_INFINITY;
		int mi = -1;
		for (int i = 0; i < probs.length; ++i)
		{
			if (probs[i] > m)
			{
				m = probs[i];
				mi = i;
			}
		}
		return mi;
	}
	
}
