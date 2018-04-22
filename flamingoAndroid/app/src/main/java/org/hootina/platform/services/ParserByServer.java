package org.hootina.platform.services;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

import org.xmlpull.v1.XmlPullParser;
import org.xmlpull.v1.XmlPullParserFactory;

public class ParserByServer {
	// ����XmlPullParser������XML�ļ�
	public static List<Server> getStudents(InputStream inStream) throws Throwable {
		List<Server> server = null;
		Server mface = null;

		XmlPullParserFactory pullFactory = XmlPullParserFactory.newInstance();
		XmlPullParser parser = pullFactory.newPullParser();
		// ��ʽ��:ʹ��Android�ṩ��ʵ�ù�����android.util.Xml
		// XmlPullParser parser = Xml.newPullParser();

		// �����ļ�������
		parser.setInput(inStream, "UTF-8");
		// ������һ���¼�
		int eventType = parser.getEventType();
		// ֻҪ�����ĵ������¼�����һֱѭ��
		while (eventType != XmlPullParser.END_DOCUMENT) {
			switch (eventType) {
			// ������ʼ�ĵ��¼�
			case XmlPullParser.START_DOCUMENT:
				server = new ArrayList<Server>();
				break;
			// ������ʼԪ���¼�
			case XmlPullParser.START_TAG:
				// ��ȡ��������ǰָ���Ԫ�ص�����
				String name = parser.getName();
				if ("Server".equals(name)) {
					// ͨ����������ȡid��Ԫ��ֵ��������student��id
					mface = new Server();
					mface.setName(parser.getAttributeValue(0));
					mface.setChatIP(parser.getAttributeValue(1));
					mface.setChatPort(parser.getAttributeValue(2));
					mface.setFileIP(parser.getAttributeValue(3));
					mface.setFilePort(parser.getAttributeValue(4));
				
				}

				break;
			// ��������Ԫ���¼�
			case XmlPullParser.END_TAG:
				//
				if ("Server".equals(parser.getName())) {
					server.add(mface);
					mface = null;
				}
				break;
			default:
				break;
			}
			eventType = parser.next();
		}
		return server;
	}
}
